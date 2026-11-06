/**
 * @file audio_reader_stream.c
 * @brief 音频流：卷事件 → 请求信箱（开启、越过标签）→ 每圈最多读一块 → 终止记录；SERVICE_BUSY 过约 10 ms 重试。
 * @note  本文件不做主机测试（FatFs、信箱、盖章、任务通知），进板级验收；每个“失败结果加卷序号 → 写什么”的判断
 *        都抽成了纯函数（audio_reader_result、audio_reader_pace、audio_reader_diag、audio_reader_range），主机测它们。
 *        除标注“播放任务调用”的接口外，全部只在音频读取任务上下文调用。
 */

#include "APP/tasks/audio_reader/audio_reader_stream.h"

#include "APP/tasks/audio_reader/audio_reader_config.h"
#include "APP/tasks/audio_reader/audio_reader_diag.h"
#include "APP/tasks/audio_reader/audio_reader_pace.h"
#include "APP/tasks/audio_reader/audio_reader_range.h"
#include "APP/tasks/audio_reader/audio_reader_result.h"

#include "APP/tasks/volume/volume_event.h"

#include "APP/apps/music/catalog/catalog.h"
#include "APP/apps/music/catalog/catalog_track_diag.h"
#include "APP/apps/music/catalog/catalog_track_open.h"

#include <string.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/queue.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/log/log_service.h"

/* 音频块一次读完，不能超过文件接口单次读写的上限（超过会返回 SERVICE_INVALID_PARAM，不截短也不拆分）。 */
_Static_assert(AUDIO_READER_BLOCK_BYTES <= SERVICE_FILESYSTEM_IO_MAX_BYTES,
               "audio block must fit one Filesystem transfer");
_Static_assert(AUDIO_READER_WATERMARK_BLOCKS < AUDIO_READER_BLOCK_COUNT,
               "the safety watermark must leave room in the block pool");

/** @brief 日志标签；串口里是 `Audio:`，标签后面有冒号。 */
static const char audio_reader_log_tag[] = "Audio";

/** @brief 重试的种类：开流（还没应答）或读块（流已打开，同一个文件位置重读）。 */
typedef enum
{
    AUDIO_READER_RETRY_NONE = 0U,
    AUDIO_READER_RETRY_OPEN,
    AUDIO_READER_RETRY_READ
} AudioReader_RetryKindTypeDef;

/* 两个长度 1 的覆盖队列做“最新值”信箱：整份原子可见，后写覆盖先写。存储静态放 DTCM。由 Boot 任务创建。 */
static uint8_t audio_reader_request_storage[sizeof(AudioReader_RequestTypeDef)];
static uint8_t audio_reader_record_storage[sizeof(AudioReader_RecordTypeDef)];
static StaticQueue_t audio_reader_request_control;
static StaticQueue_t audio_reader_record_control;
static QueueHandle_t audio_reader_request_queue;
static QueueHandle_t audio_reader_record_queue;

/* 音频读取任务入口记下的句柄；播放任务通知它（请求、块归还）。空表示任务还没跑起来，通知跳过——靠“进入循环先看一次信箱”补上。 */
static volatile TaskHandle_t audio_reader_task_handle;
static volatile AudioReader_Wake_Callback_t audio_reader_wake_callback;

/* 以下仅音频读取任务访问。 */
static AudioReader_RecordTypeDef audio_reader_record;      /* 流记录信箱内容的本地副本。 */
static Service_Filesystem_FileHandleTypeDef audio_reader_file;
static bool audio_reader_file_open;
static uint32_t audio_reader_remaining;                    /* 音频区间内尚未读出的字节数。 */
static uint32_t audio_reader_handled_sequence;             /* 已经处理过的 SD 卷序号。 */
static uint16_t audio_reader_position;                     /* 当前请求的播放列表位置，只用于日志。 */
static Catalog_TrackTypeDef audio_reader_track;            /* 约 300 B，放静态存储，不放在 4 KiB 的任务栈上。 */
static uint8_t audio_reader_head[AUDIO_READER_RANGE_HEAD_BYTES];
static uint8_t audio_reader_tail[AUDIO_READER_RANGE_TAIL_BYTES];

/* SERVICE_BUSY 重试状态：一次只会有一个在等。 */
static AudioReader_RetryKindTypeDef audio_reader_retry_kind;
static TickType_t audio_reader_retry_due;                  /* 重试时刻（tick）。 */
static uint32_t audio_reader_retry_count;                  /* 同一次开流或读块遇到 SERVICE_BUSY 的次数。 */
static AudioReader_RequestTypeDef audio_reader_retry_request; /* 开流重试时要重新跑的请求。 */
static bool audio_reader_open_busy;                        /* 本次开流尝试里有一步遇到了 SERVICE_BUSY。 */

/**
 * @brief 创建请求信箱和流记录信箱（静态）并复位流状态。
 * @note  由 Boot 任务调用一次，须早于创建播放任务和音频读取任务（播放任务此后才能调用 request）。块池由
 *        audio_reader_block_init() 另行初始化。Boot 栈只有 128 word，这里只有两次 xQueueCreateStatic。
 */
void audio_reader_stream_boot_init(void)
{
    audio_reader_request_queue = xQueueCreateStatic(1U,
                                                    sizeof(AudioReader_RequestTypeDef),
                                                    audio_reader_request_storage,
                                                    &audio_reader_request_control);
    audio_reader_record_queue = xQueueCreateStatic(1U,
                                                   sizeof(AudioReader_RecordTypeDef),
                                                   audio_reader_record_storage,
                                                   &audio_reader_record_control);
    (void)memset(&audio_reader_record, 0, sizeof(audio_reader_record));
    audio_reader_file_open = false;
    audio_reader_remaining = 0U;
    audio_reader_handled_sequence = 0U;
    audio_reader_retry_kind = AUDIO_READER_RETRY_NONE;
    audio_reader_retry_count = 0U;
}

/**
 * @brief 音频读取任务入口记下自己的句柄，播放任务此后的请求与块归还才会通知它。
 * @param[in] reader_task 音频读取任务句柄。
 * @note  之前写入的请求没有通知，由任务进入循环时先看一次信箱补上。
 */
void audio_reader_stream_bind(TaskHandle_t reader_task)
{
    audio_reader_task_handle = reader_task;
}

/**
 * @brief 注册唤醒播放任务的回调。
 * @param[in] callback 回调；NULL 表示不唤醒。
 * @note  播放任务外壳在启动时调用，回调把原因映射为自己事件槽的 FLAG。
 */
void audio_reader_stream_set_wake_callback(AudioReader_Wake_Callback_t callback)
{
    audio_reader_wake_callback = callback;
}

/**
 * @brief 唤醒播放任务。
 * @param[in] reason 唤醒原因。
 */
static void audio_reader_wake(AudioReader_WakeTypeDef reason)
{
    AudioReader_Wake_Callback_t callback = audio_reader_wake_callback;

    if (callback != NULL)
    {
        callback(reason);
    }
}

/**
 * @brief 把本地流记录整份覆盖写入信箱。
 * @param[in] wake 是否随后唤醒播放任务。
 */
static void audio_reader_publish_record(bool wake)
{
    (void)xQueueOverwrite(audio_reader_record_queue, &audio_reader_record);
    if (wake)
    {
        audio_reader_wake(AUDIO_READER_WAKE_STREAM_RECORD);
    }
}

/**
 * @brief 向音频读取任务的唯一等待点置一个唤醒位。
 * @param[in] flag 要置的 FLAG。
 * @note  任务句柄未绑定时静默忽略（任务还没跑起来，没有等待者）。
 */
static void audio_reader_notify(uint32_t flag)
{
    TaskHandle_t task = audio_reader_task_handle;

    if (task != NULL)
    {
        (void)xTaskNotifyIndexed(task, AUDIO_READER_NOTIFY_EVENT_INDEX, flag, eSetBits);
    }
}

/**
 * @brief 播放任务写请求信箱（开启、关闭、取消是同一种写入）并叫醒音频读取任务。
 * @param[in] request 请求；StreamSeq 为 0 表示无流。
 * @return true 已写入；信箱未初始化或参数为空为 false。
 * @note  播放任务调用，不阻塞。
 */
bool audio_reader_stream_request(const AudioReader_RequestTypeDef *request)
{
    if ((request == NULL) || (audio_reader_request_queue == NULL))
    {
        return false;
    }

    (void)xQueueOverwrite(audio_reader_request_queue, request);
    audio_reader_notify(AUDIO_READER_NOTIFY_FLAG_REQUEST);
    return true;
}

/**
 * @brief 播放任务读取流记录信箱的最新值，不消费。
 * @param[out] record 接收记录。
 * @return true 信箱里已有记录。
 */
bool audio_reader_stream_get_record(AudioReader_RecordTypeDef *record)
{
    if ((record == NULL) || (audio_reader_record_queue == NULL))
    {
        return false;
    }
    return xQueuePeek(audio_reader_record_queue, record, 0U) == pdTRUE;
}

/**
 * @brief 播放任务归还一块并叫醒音频读取任务（“块已归还”）。
 * @param[in] block 不再使用的块（含旧流的块）。
 * @return true 已归还。
 */
bool audio_reader_stream_return_block(AudioReader_BlockTypeDef *block)
{
    if (!audio_reader_block_release(block))
    {
        return false;
    }
    audio_reader_notify(AUDIO_READER_NOTIFY_FLAG_BLOCK_RETURNED);
    return true;
}

/**
 * @brief 取消正在等待的 SERVICE_BUSY 重试并清零计数。
 */
static void audio_reader_retry_clear(void)
{
    audio_reader_retry_kind = AUDIO_READER_RETRY_NONE;
    audio_reader_retry_count = 0U;
}

/**
 * @brief 重试时刻到了吗。
 * @param[in] now 当前 tick。
 * @return 没有重试在等，或已到时刻为真。
 */
static bool audio_reader_retry_is_due(TickType_t now)
{
    return (audio_reader_retry_kind == AUDIO_READER_RETRY_NONE) ||
           ((int32_t)(now - audio_reader_retry_due) >= 0);
}

/**
 * @brief 安排一次 SERVICE_BUSY 重试：记数、定时刻、按节拍记一行 WARN。
 * @param[in] kind 开流或读块。
 * @param[in] operation 日志里的操作名（"open" 或 "read"）。
 * @note  重试的等待用唯一的等待点（audio_reader_wait_ticks 返回距重试时刻的 tick），不用 vTaskDelay，
 *        所以重试期间新请求、卷事件仍能立刻叫醒任务。日志不刷屏：第 1 次和之后每满 AUDIO_READER_BUSY_LOG_EVERY 次一行。
 */
static void audio_reader_retry_schedule(AudioReader_RetryKindTypeDef kind, const char *operation)
{
    char text[64];

    if (audio_reader_retry_kind != kind)
    {
        audio_reader_retry_count = 0U;
    }
    audio_reader_retry_kind = kind;
    if (audio_reader_retry_count < UINT32_MAX)
    {
        audio_reader_retry_count++;
    }
    audio_reader_retry_due = xTaskGetTickCount() + pdMS_TO_TICKS(AUDIO_READER_BUSY_RETRY_MS);

    if (audio_reader_busy_log_due(audio_reader_retry_count))
    {
        (void)audio_reader_format_busy(text, sizeof(text), operation, audio_reader_position, audio_reader_retry_count);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, audio_reader_log_tag, text);
    }
}

/**
 * @brief 关闭当前 FIL（若有），并取消读块重试。
 * @note  卸载之后令牌已失效，CloseFile 只返回 INVALID_HANDLE，无害。
 */
static void audio_reader_close_file(void)
{
    if (audio_reader_file_open)
    {
        (void)Service_Filesystem_CloseFile(audio_reader_file);
        audio_reader_file_open = false;
    }
    audio_reader_remaining = 0U;
    if (audio_reader_retry_kind == AUDIO_READER_RETRY_READ)
    {
        audio_reader_retry_clear();
    }
}

/**
 * @brief 请求信箱里是否已有更新的请求。
 * @return true 有。
 */
static bool audio_reader_request_pending(void)
{
    return uxQueueMessagesWaiting(audio_reader_request_queue) != 0U;
}

/**
 * @brief 填写决定“等多久”所需的当前事实（每圈末尾调用）。
 * @param[out] input 接收事实。
 */
void audio_reader_stream_pace_input(AudioReader_PaceInputTypeDef *input)
{
    const TickType_t now = xTaskGetTickCount();

    input->RequestPending = audio_reader_request_pending();
    input->RetryActive = (audio_reader_retry_kind != AUDIO_READER_RETRY_NONE);
    input->RetryRemainingTicks = (input->RetryActive && !audio_reader_retry_is_due(now))
                                     ? (uint32_t)(audio_reader_retry_due - now)
                                     : 0U;
    input->StreamOpen = audio_reader_file_open;
    input->FreeBlocks = audio_reader_block_free_count();
    input->ReadyBlocks = audio_reader_block_ready_count();
}

/**
 * @brief 在指定偏移读满 count 字节。
 * @param[in] offset 文件偏移。
 * @param[out] buffer 接收缓冲。
 * @param[in] count 要读的字节数。
 * @return OK 表示读满；否则为对应开启结果码（短读按 IO_ERROR）。遇到 SERVICE_BUSY 时置 audio_reader_open_busy。
 */
static AudioReader_OpenResultTypeDef audio_reader_read_exact(uint32_t offset, uint8_t *buffer, uint32_t count)
{
    uint32_t got = 0U;
    Service_StatusTypeDef status;

    status = Service_Filesystem_SeekFile(audio_reader_file, (uint64_t)offset);
    if (status == SERVICE_OK)
    {
        status = Service_Filesystem_ReadFile(audio_reader_file, buffer, count, &got);
    }
    if (status != SERVICE_OK)
    {
        audio_reader_open_busy = audio_reader_open_busy || audio_reader_status_is_busy(status);
        return audio_reader_classify_open_status(status, false);
    }
    return (got == count) ? AUDIO_READER_OPEN_OK : AUDIO_READER_OPEN_IO_ERROR;
}

/**
 * @brief 读文件头尾字节、越过标签、seek 到音频区间起点加偏移。
 * @param[in] audio_offset 相对音频区间起点的字节数；越过区间时钳在区间末端。
 * @param[out] audio_bytes 音频区间长度（整段，不随偏移变化）。
 * @param[out] remaining 从落点到区间末端尚未读出的字节数；偏移被钳到末端时为 0。
 * @param[out] lseek_us 最后一次定位（到区间起点加偏移）的耗时，单位 µs；失败时无意义。
 * @return 开启结果；OK 时 FIL 已打开并定位。
 * @note  失败时 FIL 已关闭。探测是几次独立的文件调用，每次单独拿放 SD 锁，别的任务可以插进来。
 *        遇到 SERVICE_BUSY 时置 audio_reader_open_busy，由调用者决定重试。
 */
static AudioReader_OpenResultTypeDef audio_reader_probe_file(uint32_t audio_offset,
                                                             uint32_t *audio_bytes,
                                                             uint32_t *remaining,
                                                             uint32_t *lseek_us)
{
    Service_StatusTypeDef status;
    AudioReader_RangeTypeDef range;
    uint32_t position;
    uint64_t size64 = 0U;
    uint32_t size;
    uint32_t head_bytes;
    uint32_t tail_bytes;
    AudioReader_OpenResultTypeDef result;

    status = Service_Filesystem_GetFileSize(audio_reader_file, &size64);
    if (status != SERVICE_OK)
    {
        audio_reader_open_busy = audio_reader_open_busy || audio_reader_status_is_busy(status);
        result = audio_reader_classify_open_status(status, false);
    }
    else if ((size64 == 0U) || (size64 > UINT32_MAX))
    {
        result = AUDIO_READER_OPEN_BAD_FILE;
    }
    else
    {
        size = (uint32_t)size64;
        head_bytes = (size < AUDIO_READER_RANGE_HEAD_BYTES) ? size : AUDIO_READER_RANGE_HEAD_BYTES;
        tail_bytes = (size < AUDIO_READER_RANGE_TAIL_BYTES) ? size : AUDIO_READER_RANGE_TAIL_BYTES;

        result = audio_reader_read_exact(0U, audio_reader_head, head_bytes);
        if (result == AUDIO_READER_OPEN_OK)
        {
            result = audio_reader_read_exact(size - tail_bytes, audio_reader_tail, tail_bytes);
        }
        if (result == AUDIO_READER_OPEN_OK)
        {
            if (!audio_reader_range_locate(audio_reader_head, head_bytes, audio_reader_tail, tail_bytes, size, &range))
            {
                result = AUDIO_READER_OPEN_BAD_FILE;
            }
            else
            {
                const uint32_t seek_start = catalog_track_diag_begin();

                position = audio_reader_range_seek_position(&range, audio_offset);
                status = Service_Filesystem_SeekFile(audio_reader_file, (uint64_t)position);
                *lseek_us = catalog_track_diag_elapsed_us(seek_start);
                if (status == SERVICE_OK)
                {
                    *audio_bytes = range.End - range.Start;
                    *remaining = range.End - position;
                }
                else
                {
                    audio_reader_open_busy = audio_reader_open_busy || audio_reader_status_is_busy(status);
                    result = audio_reader_classify_open_status(status, false);
                }
            }
        }
    }

    if (result != AUDIO_READER_OPEN_OK)
    {
        audio_reader_close_file();
    }
    return result;
}

/**
 * @brief 按请求打开曲目，填写流记录的开启部分。
 * @param[in] request 非“无流”的请求。
 * @return 开启结果；OK 时 FIL 已打开，audio_reader_record 的参数已填。本次尝试遇到 SERVICE_BUSY 时
 *         audio_reader_open_busy 为真，返回值作废（调用者重试，不应答）。
 * @note  曲库没好（代次 0：扫描中、卷变了、没发布）立刻应答 NOT_READY，不等“曲库就绪”（#194 第 15 条）。
 *        先按曲库里的文件定位符打开，核对不符退回按路径打开（STOR-17）；打开成功后记一行 `Stream` INFO。
 *        请求的 AudioOffset 钳到音频区间内后定位（0 为区间起点）；钳到末端时流记录同时带 ENDED。代次 0 表示“当前”。
 *        位置超出列表按 STALE。无法区分“文件不存在”与“打开时 I/O 错误”，打开失败一律 BAD_FILE（C3 的“打不开”）。
 *        BAD_FILE 时仍填写 Generation 与 TrackCount（位置已通过 STALE 校验），供播放任务跳过坏文件；其余失败二者为 0。
 */
static AudioReader_OpenResultTypeDef audio_reader_open(const AudioReader_RequestTypeDef *request)
{
    uint16_t track_count;
    bool found;
    uint32_t audio_bytes = 0U;
    uint32_t remaining = 0U;
    uint32_t open_us = 0U;
    uint32_t lseek_us = 0U;
    Service_StatusTypeDef status;
    AudioReader_OpenResultTypeDef result;

    audio_reader_open_busy = false;
    audio_reader_position = request->Position;

    /* 代次、条数、路径和定位符一次读全（同一把锁内）：中途重扫或拔卡不会读到半新半旧的内容。 */
    found = catalog_read_track(request->Position, &audio_reader_track);
    if (audio_reader_track.Generation == 0U)
    {
        return AUDIO_READER_OPEN_NOT_READY;
    }
    track_count = audio_reader_track.Count;
    if (track_count == 0U)
    {
        return AUDIO_READER_OPEN_NO_TRACK;
    }
    if (((request->Generation != 0U) && (request->Generation != audio_reader_track.Generation)) ||
        (request->Position >= track_count) || !found)
    {
        return AUDIO_READER_OPEN_STALE;
    }

    /* 位置已通过 STALE 校验：此后即使 BAD_FILE，播放任务也要靠代次与曲目数继续跳过坏文件。 */
    audio_reader_record.Generation = audio_reader_track.Generation;
    audio_reader_record.TrackCount = track_count;

    if (AUDIO_READER_HOOK_LOCATOR_MISS != 0U)
    {
        /* 定位符失效测试钩子（R4.4）：全零表示“无定位符”，Filesystem 判核对不符，走真实的退回按路径打开。 */
        (void)memset(&audio_reader_track.Locator, 0, sizeof(audio_reader_track.Locator));
    }

    status = catalog_track_open(request->Position, &audio_reader_track, &audio_reader_file, &open_us);
    if (status != SERVICE_OK)
    {
        audio_reader_open_busy = audio_reader_status_is_busy(status);
        return audio_reader_classify_open_status(status, true);
    }
    audio_reader_file_open = true;

    result = audio_reader_probe_file(request->AudioOffset, &audio_bytes, &remaining, &lseek_us);
    if (result == AUDIO_READER_OPEN_OK)
    {
        /* 曲目或跳转边界上记一行（DIAG-5）；off= 取请求里的 AudioOffset，不是钳位后的落点。 */
        catalog_track_diag_stream_done(request->Position, request->AudioOffset, open_us, lseek_us);
        audio_reader_remaining = remaining;
        audio_reader_record.AudioBytes = audio_bytes;
        if (remaining == 0U)
        {
            /* 偏移被钳到区间末端：没有字节可读，直接读尽（不是读错误），不占块。 */
            audio_reader_close_file();
            audio_reader_record.End = AUDIO_READER_END_ENDED;
        }
    }
    return result;
}

/**
 * @brief 跑一次开流并应答；遇到 SERVICE_BUSY 就安排重试、先不应答。
 * @param[in] request 非“无流”的请求；记录里的 StreamSeq 已填。
 */
static void audio_reader_start_stream(const AudioReader_RequestTypeDef *request)
{
    audio_reader_record.OpenResult = audio_reader_open(request);

    if (audio_reader_open_busy)
    {
        /* 文件槽暂时用尽：不是文件错误，不应答、不当坏文件跳过；过约 10 ms 重来，T_data 兜底。 */
        audio_reader_close_file();
        audio_reader_record.OpenResult = AUDIO_READER_OPEN_PENDING;
        audio_reader_record.Generation = 0U;
        audio_reader_record.TrackCount = 0U;
        audio_reader_record.AudioBytes = 0U;
        audio_reader_record.End = AUDIO_READER_END_NONE;
        audio_reader_retry_request = *request;
        audio_reader_retry_schedule(AUDIO_READER_RETRY_OPEN, "open");
        return;
    }

    audio_reader_retry_clear();
    if (audio_reader_record.OpenResult != AUDIO_READER_OPEN_OK)
    {
        if (audio_reader_record.OpenResult != AUDIO_READER_OPEN_BAD_FILE)
        {
            audio_reader_record.Generation = 0U;
            audio_reader_record.TrackCount = 0U;
        }
        audio_reader_record.AudioBytes = 0U;
    }
    audio_reader_publish_record(true);
}

/**
 * @brief 处理请求信箱：关闭旧流，开启新流并应答；到点的开流重试也在这里重跑（每圈第二步）。
 * @note  开启 ≤ 等 SD 锁 + 按定位符打开 + 标签探测（读头、读尾），目标约 ≤ 30 ms。消抖中不暂缓：文件调用在
 *        Filesystem 入口等闸，开流的调用慢约 30 ms 后正常返回，或卸载后返回失败（按 NOT_READY 应答）。
 *        新请求取代任何在等的重试。
 */
void audio_reader_stream_process_request(void)
{
    AudioReader_RequestTypeDef request;

    if (xQueueReceive(audio_reader_request_queue, &request, 0U) == pdTRUE)
    {
        audio_reader_retry_clear();
        audio_reader_close_file();
        (void)memset(&audio_reader_record, 0, sizeof(audio_reader_record));

        if (request.StreamSeq == 0U)
        {
            audio_reader_publish_record(false);
            return;
        }
        audio_reader_record.StreamSeq = request.StreamSeq;
    }
    else if ((audio_reader_retry_kind == AUDIO_READER_RETRY_OPEN) && audio_reader_retry_is_due(xTaskGetTickCount()))
    {
        request = audio_reader_retry_request;
    }
    else
    {
        return;
    }

    audio_reader_start_stream(&request);
}

/**
 * @brief 最多读一块并交给播放任务（每圈第三步）。
 * @note  一块对应一次 SDMMC DMA，也是取消的上界之一（取消 ≤ 等 SD 锁 + 一次块读取）。读完后若请求信箱已有
 *        新请求，说明流被取代，这个半读的块不入就绪队列，直接放回空闲队列。ENDED 只在最后一块入队之后才写。
 *
 *        **读到 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 时**：先对同一个句柄 `CloseFile`（结果不看）。它在
 *        Filesystem 入口等闸，所以卡刚拔、边沿已到而 30 ms 消抖或对账还没结束时，会一直等到本轮对账结束才返回，
 *        不会把拔卡误报成读错误；闸开着时立刻返回。然后看 SD 卷：序号变了或卷已不挂，是卡没了，流停住（FIL 已关，
 *        不空转），等卷事件写 `CARD_REMOVED`；序号没变且卷仍挂着，是卡还在、设备出错（例如标签读取传输出错使设备
 *        进 ERROR），没有边沿就没有卷事件，立刻按读错误报，不等 T_data（#195 第 9 条、#211）。回到等待点等卷事件
 *        （#194 第 11 条的字面）只适用于前一种；后一种没有事件可等，会静音到 T_data，所以按 #211 的版本做。
 *        判断在 `audio_reader_classify_read()`（主机测试）。
 *
 *        读到 `SERVICE_BUSY`（文件槽暂时用尽）不是文件错误：放回这个块，过约 10 ms 在同一个文件位置重读。
 */
void audio_reader_stream_read_block(void)
{
    AudioReader_BlockTypeDef *block;
    AudioReader_ReadVerdictTypeDef verdict;
    Volume_StatusTypeDef volume;
    Service_StatusTypeDef status;
    uint32_t want;
    uint32_t got = 0U;

    if (!audio_reader_file_open)
    {
        return;
    }
    if (!audio_reader_retry_is_due(xTaskGetTickCount()))
    {
        return;
    }
    block = audio_reader_block_acquire();
    if (block == NULL)
    {
        return;
    }

    want = (audio_reader_remaining < AUDIO_READER_BLOCK_BYTES) ? audio_reader_remaining : AUDIO_READER_BLOCK_BYTES;
    status = Service_Filesystem_ReadFile(audio_reader_file, block->Data, want, &got);

    if (audio_reader_request_pending())
    {
        (void)audio_reader_block_release(block);
        return;
    }
    if (Service_Filesystem_StatusIsVolumeUnavailable(status))
    {
        /* 先放掉这个句柄再下结论：CloseFile 在 Filesystem 入口等闸，消抖或对账还没结束时会等到结束，闸开着时立刻返回。
         * 不对它重试、不期待它成功（槽靠卸载释放）。 */
        (void)Service_Filesystem_CloseFile(audio_reader_file);
        audio_reader_file_open = false;
    }
    volume = volume_status(VOLUME_ID_SD);
    verdict = audio_reader_classify_read(status, got, volume.Mounted, volume.Sequence, audio_reader_handled_sequence);

    if (verdict == AUDIO_READER_READ_RETRY)
    {
        (void)audio_reader_block_release(block);
        audio_reader_retry_schedule(AUDIO_READER_RETRY_READ, "read");
        return;
    }
    if (verdict == AUDIO_READER_READ_STALL)
    {
        /* 卡没了或正在对账：不是文件错误，不写 READ_ERROR、不缓存。FIL 已关，记录保持不变；
         * 卷任务卸载后发卷事件，序号变了由 audio_reader_stream_apply_volume() 写 CARD_REMOVED（#195 第 9 条）。 */
        (void)audio_reader_block_release(block);
        audio_reader_remaining = 0U;
        audio_reader_retry_clear();
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO,
                               audio_reader_log_tag,
                               "Audio read interrupted: volume unavailable, waiting for volume event.");
        return;
    }
    if (verdict == AUDIO_READER_READ_ERROR)
    {
        (void)audio_reader_block_release(block);
        audio_reader_close_file();
        audio_reader_retry_clear();
        audio_reader_record.End = AUDIO_READER_END_READ_ERROR;
        audio_reader_publish_record(true);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, audio_reader_log_tag, "Audio stream read failed.");
        return;
    }

    audio_reader_retry_clear();
    block->StreamSeq = audio_reader_record.StreamSeq;
    block->ValidBytes = got;
    audio_reader_remaining -= got;
    (void)audio_reader_block_publish(block);
    audio_reader_wake(AUDIO_READER_WAKE_BLOCK_READY);

    if (audio_reader_remaining == 0U)
    {
        audio_reader_close_file();
        audio_reader_record.End = AUDIO_READER_END_ENDED;
        audio_reader_publish_record(true);
    }
}

/**
 * @brief 处理 SD 卷事件（每圈第一步）：序号变了，旧的全部作废；当前流仍被播放任务持有就写 CARD_REMOVED。
 * @note  每圈都调用（比对一个字，没变就立刻返回），所以丢叫醒、合并叫醒、晚订阅都不会漏。
 *        拔卡由本任务自己收尾，卷任务不等它：卸载已使旧令牌失效，晚一点收尾也碰不到旧卡，之后 `CloseFile`
 *        只返回 `INVALID_HANDLE`，无害。“先写 CARD_REMOVED 再卸载”的顺序不再保证（#194 第 9 条）。
 *        “当前流”：已为某个非 0 流序号成功开启、播放任务尚未以无流请求或新流序号释放它、也没有以读错误或拔卡终止；
 *        已读完（ENDED）的流也算，所以文件已读完而播放任务仍在从内存放音时拔卡同样立即停止。
 *        换卡时序号跳 2，同样走这里（换卡视同拔卡）。若拔卡处理时仍有新流请求排队，旧记录照常写 CARD_REMOVED，
 *        播放任务因该流已释放而忽略它，新请求的开启因曲库代次 0 以 NOT_READY 结束，播放仍会停止；此情形可接受。
 *        判断在 `audio_reader_end_on_volume_change()`（主机测试）。
 *        写完之后记一行 `CARD_REMOVED lat=` 常开日志（R4.10）：时延相对卷任务记下的卡检测第一个边沿，在固件里算好。
 */
void audio_reader_stream_apply_volume(void)
{
    const Volume_StatusTypeDef volume = volume_status(VOLUME_ID_SD);
    const uint32_t previous = audio_reader_handled_sequence;
    uint32_t edge_tick = 0U;
    uint32_t now_tick;
    bool edge_known;
    char text[96];

    if (volume.Sequence == previous)
    {
        return;
    }
    audio_reader_handled_sequence = volume.Sequence;

    if (audio_reader_end_on_volume_change(&audio_reader_record, volume.Sequence, previous) != AUDIO_READER_END_CARD_REMOVED)
    {
        return;
    }

    audio_reader_close_file();
    audio_reader_retry_clear();
    audio_reader_record.End = AUDIO_READER_END_CARD_REMOVED;
    audio_reader_publish_record(true);

    now_tick = (uint32_t)xTaskGetTickCount();
    edge_known = volume_event_published_edge(VOLUME_ID_SD, &edge_tick);
    (void)audio_reader_format_removed(text,
                                      sizeof(text),
                                      edge_known,
                                      (now_tick - edge_tick) * (uint32_t)portTICK_PERIOD_MS,
                                      previous,
                                      volume.Sequence,
                                      audio_reader_record.StreamSeq);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, audio_reader_log_tag, text);
}
