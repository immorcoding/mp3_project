/**
 * @file storage_audio_stream.c
 * @brief Storage 音频流主循环：请求信箱 → 开启（越过标签）→ 每圈最多读一块 → 终止记录。
 * @note  本文件不做主机测试（FatFs、信箱、盖章），进板级验收；纯函数部分在 storage_audio_range。
 *        除标注“播放任务调用”的接口外，全部只在 Storage Task 普通上下文调用。
 */

#include "storage_audio_stream.h"

#include "storage_audio_range.h"

#include "APP/tasks/storage/catalog/storage_catalog.h"
#include "APP/tasks/storage/catalog/storage_sheet.h"
#include "APP/tasks/storage/storage_task_config.h"

#include <string.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/queue.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/log/log_service.h"

static const char storage_audio_log_tag[] = "Audio";

/* 两个长度 1 的覆盖队列做“最新值”信箱：整份原子可见，后写覆盖先写。存储静态放 DTCM。 */
static uint8_t storage_audio_request_storage[sizeof(Storage_AudioRequestTypeDef)];
static uint8_t storage_audio_record_storage[sizeof(Storage_AudioRecordTypeDef)];
static StaticQueue_t storage_audio_request_control;
static StaticQueue_t storage_audio_record_control;
static QueueHandle_t storage_audio_request_queue;
static QueueHandle_t storage_audio_record_queue;

static TaskHandle_t storage_audio_storage_task;
static volatile Storage_AudioWake_Callback_t storage_audio_wake_callback;

/* 以下仅 Storage Task 访问。 */
static Storage_AudioRecordTypeDef storage_audio_record;        /* 流记录信箱内容的本地副本。 */
static Service_Filesystem_FileHandleTypeDef storage_audio_file;
static bool storage_audio_file_open;
static uint32_t storage_audio_remaining;                       /* 音频区间内尚未读出的字节数。 */
static char storage_audio_path[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];
static uint8_t storage_audio_head[STORAGE_AUDIO_RANGE_HEAD_BYTES];
static uint8_t storage_audio_tail[STORAGE_AUDIO_RANGE_TAIL_BYTES];

/**
 * @brief 创建信箱并绑定 Storage Task 句柄，复位流状态。
 * @param[in] storage_task Storage Task 句柄，供 request/return_block 发通知。
 * @note  Storage Task 启动时调用一次，须早于播放任务首次调用 request。块池由
 *        storage_audio_block_init() 另行初始化。
 */
void storage_audio_stream_init(TaskHandle_t storage_task)
{
    storage_audio_storage_task = storage_task;
    storage_audio_request_queue = xQueueCreateStatic(1U,
                                                     sizeof(Storage_AudioRequestTypeDef),
                                                     storage_audio_request_storage,
                                                     &storage_audio_request_control);
    storage_audio_record_queue = xQueueCreateStatic(1U,
                                                    sizeof(Storage_AudioRecordTypeDef),
                                                    storage_audio_record_storage,
                                                    &storage_audio_record_control);
    (void)memset(&storage_audio_record, 0, sizeof(storage_audio_record));
    storage_audio_file_open = false;
    storage_audio_remaining = 0U;
}

/**
 * @brief 注册唤醒播放任务的回调。
 * @param[in] callback 回调；NULL 表示不唤醒。
 * @note  播放任务外壳在启动时调用，回调把原因映射为自己事件槽的 FLAG。
 */
void storage_audio_stream_set_wake_callback(Storage_AudioWake_Callback_t callback)
{
    storage_audio_wake_callback = callback;
}

/**
 * @brief 唤醒播放任务。
 * @param[in] reason 唤醒原因。
 */
static void storage_audio_wake(Storage_AudioWakeTypeDef reason)
{
    Storage_AudioWake_Callback_t callback = storage_audio_wake_callback;

    if (callback != NULL)
    {
        callback(reason);
    }
}

/**
 * @brief 把本地流记录整份覆盖写入信箱。
 * @param[in] wake 是否随后唤醒播放任务。
 */
static void storage_audio_publish_record(bool wake)
{
    (void)xQueueOverwrite(storage_audio_record_queue, &storage_audio_record);
    if (wake)
    {
        storage_audio_wake(STORAGE_AUDIO_WAKE_STREAM_RECORD);
    }
}

/**
 * @brief 播放任务写请求信箱（开启、关闭、取消是同一种写入）并置 Storage 的“流请求”FLAG。
 * @param[in] request 请求；StreamSeq 为 0 表示无流。
 * @return true 已写入；信箱未初始化或参数为空为 false。
 * @note  播放任务调用，不阻塞。
 */
bool storage_audio_stream_request(const Storage_AudioRequestTypeDef *request)
{
    if ((request == NULL) || (storage_audio_request_queue == NULL))
    {
        return false;
    }

    (void)xQueueOverwrite(storage_audio_request_queue, request);
    if (storage_audio_storage_task != NULL)
    {
        (void)xTaskNotifyIndexed(storage_audio_storage_task,
                                 STORAGE_NOTIFY_EVENT,
                                 STORAGE_NOTIFY_FLAG_STREAM_REQUEST,
                                 eSetBits);
    }
    return true;
}

/**
 * @brief 播放任务读取流记录信箱的最新值，不消费。
 * @param[out] record 接收记录。
 * @return true 信箱里已有记录。
 */
bool storage_audio_stream_get_record(Storage_AudioRecordTypeDef *record)
{
    if ((record == NULL) || (storage_audio_record_queue == NULL))
    {
        return false;
    }
    return xQueuePeek(storage_audio_record_queue, record, 0U) == pdTRUE;
}

/**
 * @brief 播放任务归还一块并置 Storage 的“块已归还”FLAG。
 * @param[in] block 不再使用的块（含旧流的块）。
 * @return true 已归还。
 */
bool storage_audio_stream_return_block(Storage_AudioBlockTypeDef *block)
{
    if (!storage_audio_block_release(block))
    {
        return false;
    }
    if (storage_audio_storage_task != NULL)
    {
        (void)xTaskNotifyIndexed(storage_audio_storage_task,
                                 STORAGE_NOTIFY_EVENT,
                                 STORAGE_NOTIFY_FLAG_BLOCK_RETURNED,
                                 eSetBits);
    }
    return true;
}

/**
 * @brief 是否有打开的音频流（FIL 仍打开）。
 * @return true 有。此时 storage_task 不提供 Flash 回收机会。
 */
bool storage_audio_stream_is_open(void)
{
    return storage_audio_file_open;
}

/**
 * @brief 关闭当前 FIL（若有）。
 */
static void storage_audio_close_file(void)
{
    if (storage_audio_file_open)
    {
        (void)Service_Filesystem_CloseFile(storage_audio_file);
        storage_audio_file_open = false;
    }
    storage_audio_remaining = 0U;
}

/**
 * @brief 请求信箱里是否已有更新的请求。
 * @return true 有。
 */
static bool storage_audio_request_pending(void)
{
    return uxQueueMessagesWaiting(storage_audio_request_queue) != 0U;
}

/**
 * @brief 本圈是否还有工作，决定主循环能否阻塞等待。
 * @param[in] media 介质状态。
 * @return true 有待处理请求，或流已打开且有空闲块可读。消抖中为 false。
 */
bool storage_audio_stream_has_work(Storage_AudioMediaTypeDef media)
{
    if (media == STORAGE_AUDIO_MEDIA_DEBOUNCING)
    {
        return false;
    }
    if (storage_audio_request_pending())
    {
        return true;
    }
    return storage_audio_file_open && (storage_audio_block_free_count() != 0U);
}

/**
 * @brief 把 Service 状态收成开启结果。
 * @param[in] status 打开/seek/读取的 Service 状态。
 * @param[in] open_failure true 表示打开文件本身失败（无法区分缺失与损坏，按坏文件）。
 * @return 开启结果码。
 */
static Storage_AudioOpenResultTypeDef storage_audio_map_open_status(Service_StatusTypeDef status,
                                                                    bool open_failure)
{
    switch (status)
    {
        case SERVICE_NO_MEDIA:
        case SERVICE_NOT_READY:
        case SERVICE_NO_FILESYSTEM:
            return STORAGE_AUDIO_OPEN_NOT_READY;
        case SERVICE_TIMEOUT:
            return STORAGE_AUDIO_OPEN_IO_ERROR;
        case SERVICE_INVALID_PARAM:
            return STORAGE_AUDIO_OPEN_BAD_FILE;
        default:
            return open_failure ? STORAGE_AUDIO_OPEN_BAD_FILE : STORAGE_AUDIO_OPEN_IO_ERROR;
    }
}

/**
 * @brief 在指定偏移读满 count 字节。
 * @param[in] offset 文件偏移。
 * @param[out] buffer 接收缓冲。
 * @param[in] count 要读的字节数。
 * @return OK 表示读满；否则为对应开启结果码（短读按 IO_ERROR）。
 */
static Storage_AudioOpenResultTypeDef storage_audio_read_exact(uint32_t offset,
                                                               uint8_t *buffer,
                                                               uint32_t count)
{
    uint32_t got = 0U;
    Service_StatusTypeDef status;

    status = Service_Filesystem_SeekFile(storage_audio_file, (uint64_t)offset);
    if (status == SERVICE_OK)
    {
        status = Service_Filesystem_ReadFile(storage_audio_file, buffer, count, &got);
    }
    if (status != SERVICE_OK)
    {
        return storage_audio_map_open_status(status, false);
    }
    return (got == count) ? STORAGE_AUDIO_OPEN_OK : STORAGE_AUDIO_OPEN_IO_ERROR;
}

/**
 * @brief 读文件头尾字节、越过标签、seek 到音频起点。
 * @param[out] audio_bytes 音频区间长度。
 * @return 开启结果；OK 时 FIL 已打开并定位。
 * @note  失败时 FIL 已关闭。
 */
static Storage_AudioOpenResultTypeDef storage_audio_probe_file(uint32_t *audio_bytes)
{
    Service_StatusTypeDef status;
    Storage_AudioRangeTypeDef range;
    uint64_t size64 = 0U;
    uint32_t size;
    uint32_t head_bytes;
    uint32_t tail_bytes;
    Storage_AudioOpenResultTypeDef result;

    status = Service_Filesystem_GetFileSize(storage_audio_file, &size64);
    if (status != SERVICE_OK)
    {
        result = storage_audio_map_open_status(status, false);
    }
    else if ((size64 == 0U) || (size64 > UINT32_MAX))
    {
        result = STORAGE_AUDIO_OPEN_BAD_FILE;
    }
    else
    {
        size = (uint32_t)size64;
        head_bytes = (size < STORAGE_AUDIO_RANGE_HEAD_BYTES) ? size : STORAGE_AUDIO_RANGE_HEAD_BYTES;
        tail_bytes = (size < STORAGE_AUDIO_RANGE_TAIL_BYTES) ? size : STORAGE_AUDIO_RANGE_TAIL_BYTES;

        result = storage_audio_read_exact(0U, storage_audio_head, head_bytes);
        if (result == STORAGE_AUDIO_OPEN_OK)
        {
            result = storage_audio_read_exact(size - tail_bytes, storage_audio_tail, tail_bytes);
        }
        if (result == STORAGE_AUDIO_OPEN_OK)
        {
            if (!storage_audio_range_locate(storage_audio_head, head_bytes, storage_audio_tail, tail_bytes, size, &range))
            {
                result = STORAGE_AUDIO_OPEN_BAD_FILE;
            }
            else
            {
                status = Service_Filesystem_SeekFile(storage_audio_file, (uint64_t)range.Start);
                if (status == SERVICE_OK)
                {
                    *audio_bytes = range.End - range.Start;
                }
                else
                {
                    result = storage_audio_map_open_status(status, false);
                }
            }
        }
    }

    if (result != STORAGE_AUDIO_OPEN_OK)
    {
        storage_audio_close_file();
    }
    return result;
}

/**
 * @brief 按请求打开曲目，填写流记录的开启部分。
 * @param[in] request 非“无流”的请求。
 * @param[in] media 介质状态（非消抖）。
 * @return 开启结果；OK 时 FIL 已打开，storage_audio_record 的参数已填。
 * @note  代次 0 表示“当前”。位置超出列表按 STALE。无法区分“文件不存在”与“打开时 I/O 错误”，
 *        打开失败一律 BAD_FILE（C3 的“打不开”）。BAD_FILE 时仍填写 Generation 与 TrackCount（位置已通过 STALE 校验），供播放任务跳过坏文件；其余失败二者为 0。
 */
static Storage_AudioOpenResultTypeDef storage_audio_open(const Storage_AudioRequestTypeDef *request,
                                                         Storage_AudioMediaTypeDef media)
{
    uint32_t sheet_generation;
    uint16_t track_count;
    uint16_t catalog_index;
    uint32_t audio_bytes = 0U;
    Service_StatusTypeDef status;
    Storage_AudioOpenResultTypeDef result;

    if (media != STORAGE_AUDIO_MEDIA_READY)
    {
        return STORAGE_AUDIO_OPEN_NOT_READY;
    }

    sheet_generation = storage_sheet_generation();
    if ((sheet_generation == 0U) || (sheet_generation != storage_catalog_generation()))
    {
        return STORAGE_AUDIO_OPEN_NOT_READY;
    }
    track_count = storage_catalog_index_num();
    if (track_count == 0U)
    {
        return STORAGE_AUDIO_OPEN_NO_TRACK;
    }
    if (((request->Generation != 0U) && (request->Generation != sheet_generation)) ||
        (request->Position >= track_count))
    {
        return STORAGE_AUDIO_OPEN_STALE;
    }

    catalog_index = storage_sheet_catalog_index(request->Position);
    if ((catalog_index == UINT16_MAX) ||
        (storage_catalog_copy_path(catalog_index, storage_audio_path, sizeof(storage_audio_path)) != STORAGE_OK))
    {
        return STORAGE_AUDIO_OPEN_STALE;
    }

    /* 位置已通过 STALE 校验：此后即使 BAD_FILE，播放任务也要靠代次与曲目数继续跳过坏文件。 */
    storage_audio_record.Generation = sheet_generation;
    storage_audio_record.TrackCount = track_count;

    status = Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                         storage_audio_path,
                                         SERVICE_FILESYSTEM_FILE_MODE_READ,
                                         &storage_audio_file);
    if (status != SERVICE_OK)
    {
        return storage_audio_map_open_status(status, true);
    }
    storage_audio_file_open = true;

    result = storage_audio_probe_file(&audio_bytes);
    if (result == STORAGE_AUDIO_OPEN_OK)
    {
        storage_audio_remaining = audio_bytes;
        storage_audio_record.AudioBytes = audio_bytes;
    }
    return result;
}

/**
 * @brief 处理请求信箱：关闭旧流，开启新流并应答（主循环第一步）。
 * @param[in] media 介质状态。消抖中暂缓，不消费请求、不应答。
 * @note  开启 ≤ f_open + 标签探测（读头、读尾）；一次读块在随后的 read_block。
 */
void storage_audio_stream_process_request(Storage_AudioMediaTypeDef media)
{
    Storage_AudioRequestTypeDef request;

    if (media == STORAGE_AUDIO_MEDIA_DEBOUNCING)
    {
        return;
    }
    if (xQueueReceive(storage_audio_request_queue, &request, 0U) != pdTRUE)
    {
        return;
    }

    storage_audio_close_file();
    (void)memset(&storage_audio_record, 0, sizeof(storage_audio_record));

    if (request.StreamSeq == 0U)
    {
        storage_audio_publish_record(false);
        return;
    }

    storage_audio_record.StreamSeq = request.StreamSeq;
    storage_audio_record.OpenResult = storage_audio_open(&request, media);
    if (storage_audio_record.OpenResult != STORAGE_AUDIO_OPEN_OK)
    {
        if (storage_audio_record.OpenResult != STORAGE_AUDIO_OPEN_BAD_FILE)
        {
            storage_audio_record.Generation = 0U;
            storage_audio_record.TrackCount = 0U;
        }
        storage_audio_record.AudioBytes = 0U;
    }
    storage_audio_publish_record(true);
}

/**
 * @brief 最多读一块并交给播放任务（主循环第三步）。
 * @param[in] media 介质状态。消抖中暂缓补块。
 * @note  一块对应一次 SDMMC DMA，也是取消的上界。读完后若请求信箱已有新请求，说明流被取代，
 *        这个半读的块不入就绪队列，直接放回空闲队列。ENDED 只在最后一块入队之后才写。
 */
void storage_audio_stream_read_block(Storage_AudioMediaTypeDef media)
{
    Storage_AudioBlockTypeDef *block;
    Service_StatusTypeDef status;
    uint32_t want;
    uint32_t got = 0U;

    if ((media != STORAGE_AUDIO_MEDIA_READY) || !storage_audio_file_open)
    {
        return;
    }
    block = storage_audio_block_acquire();
    if (block == NULL)
    {
        return;
    }

    want = (storage_audio_remaining < STORAGE_AUDIO_BLOCK_BYTES) ? storage_audio_remaining
                                                                  : STORAGE_AUDIO_BLOCK_BYTES;
    status = Service_Filesystem_ReadFile(storage_audio_file, block->Data, want, &got);

    if (storage_audio_request_pending())
    {
        (void)storage_audio_block_release(block);
        return;
    }
    if ((status != SERVICE_OK) || (got == 0U))
    {
        (void)storage_audio_block_release(block);
        storage_audio_close_file();
        storage_audio_record.End = STORAGE_AUDIO_END_READ_ERROR;
        storage_audio_publish_record(true);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, storage_audio_log_tag, "Audio stream read failed.");
        return;
    }

    block->StreamSeq = storage_audio_record.StreamSeq;
    block->ValidBytes = got;
    storage_audio_remaining -= got;
    (void)storage_audio_block_publish(block);
    storage_audio_wake(STORAGE_AUDIO_WAKE_BLOCK_READY);

    if (storage_audio_remaining == 0U)
    {
        storage_audio_close_file();
        storage_audio_record.End = STORAGE_AUDIO_END_ENDED;
        storage_audio_publish_record(true);
    }
}

/**
 * @brief 判断当前流记录是否仍被播放任务持有（已成功开启、尚未终止或仅读尽）。
 * @retval true  StreamSeq 非 0、OpenResult 为 OK，且 End 为 NONE（FIL 仍开）或 ENDED（读尽）。
 * @retval false 从未开启、开启失败，或已以 READ_ERROR / CARD_REMOVED 终止。
 */
static bool storage_audio_stream_held_by_playback(void)
{
    return (storage_audio_record.StreamSeq != 0U) &&
           (storage_audio_record.OpenResult == STORAGE_AUDIO_OPEN_OK) &&
           ((storage_audio_record.End == STORAGE_AUDIO_END_NONE) ||
            (storage_audio_record.End == STORAGE_AUDIO_END_ENDED));
}

/**
 * @brief 确认拔卡（或换卡）时，若当前流仍被播放任务持有，先关闭 FIL，再写 {当前流序号, CARD_REMOVED} 并唤醒播放任务。
 * @note  须在卸载 FatFs 卷之前调用。“当前流”的完整定义见 APP/tasks/storage/README.md 音频流分区的拔卡条目。
 */
void storage_audio_stream_card_removed(void)
{
    if (!storage_audio_stream_held_by_playback())
    {
        return;
    }

    storage_audio_close_file();
    storage_audio_record.End = STORAGE_AUDIO_END_CARD_REMOVED;
    storage_audio_publish_record(true);
}
