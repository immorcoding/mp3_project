/**
 * @file playback_task.c
 * @brief 播放任务外壳：把命令、Storage 音频流、播放模块与 Platform 音频事件接到纯 C 状态机（#71 B1、B2、K2、A5）。
 * @note  本文件不做主机测试，进板级验收；逻辑由 Tests/playback_task 覆盖状态机。
 *        不碰 FatFs/LVGL，不同步等待 Storage，发送不阻塞，没有运行期 malloc（B2）：命令 FIFO、状态信箱都是
 *        静态创建的 FreeRTOS 队列，位于 DTCM .bss。
 */

#include "APP/tasks/playback/playback_task.h"

#include <stddef.h>
#include <string.h>

#include "APP/tasks/playback/playback_contract.h"
#include "APP/tasks/playback/playback_machine.h"
#include "APP/tasks/storage/stream/storage_audio_block.h"
#include "APP/tasks/storage/stream/storage_audio_stream.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/queue.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Platform/audio/platform_audio.h"
#include "Service/log/log_service.h"
#include "Service/playback/playback_service.h"

_Static_assert((unsigned)PLAYBACK_NOTIFY_COUNT <= (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES,
               "Playback Task notify slots exceed FreeRTOS array length");
_Static_assert(configTICK_RATE_HZ == 1000U, "playback_task_now_ms() assumes a 1 kHz tick");
_Static_assert((PLAYBACK_MACHINE_STALE_BLOCK_LIMIT == STORAGE_AUDIO_BLOCK_COUNT) &&
                   (PLAYBACK_MACHINE_FEED_BLOCK_LIMIT == STORAGE_AUDIO_BLOCK_COUNT),
               "状态机的块循环上限须与块池块数 STORAGE_AUDIO_BLOCK_COUNT 同值");

/**
 * @brief 断言状态机自带类型与 Storage 类型的某个字段一一对应（宽度与偏移都相同）。
 * @note  #78 决定：状态机携带 storage_audio_stream.h 类型的 1:1 副本，映射在本外壳里做；
 *        任何一边增删字段、改变宽度或顺序，这里在编译期报错，提醒同步另一边。
 */
#define PLAYBACK_TASK_ASSERT_FIELD(machine_type, storage_type, field)                                      \
    _Static_assert(sizeof(((machine_type *)0)->field) == sizeof(((storage_type *)0)->field),               \
                   #machine_type "." #field " width differs from " #storage_type);                          \
    _Static_assert(offsetof(machine_type, field) == offsetof(storage_type, field),                         \
                   #machine_type "." #field " offset differs from " #storage_type)

_Static_assert(sizeof(Playback_MachineRequestTypeDef) == sizeof(Storage_AudioRequestTypeDef),
               "Playback_MachineRequestTypeDef must mirror Storage_AudioRequestTypeDef");
PLAYBACK_TASK_ASSERT_FIELD(Playback_MachineRequestTypeDef, Storage_AudioRequestTypeDef, StreamSeq);
PLAYBACK_TASK_ASSERT_FIELD(Playback_MachineRequestTypeDef, Storage_AudioRequestTypeDef, Generation);
PLAYBACK_TASK_ASSERT_FIELD(Playback_MachineRequestTypeDef, Storage_AudioRequestTypeDef, Position);

_Static_assert(sizeof(Playback_MachineRecordTypeDef) == sizeof(Storage_AudioRecordTypeDef),
               "Playback_MachineRecordTypeDef must mirror Storage_AudioRecordTypeDef");
PLAYBACK_TASK_ASSERT_FIELD(Playback_MachineRecordTypeDef, Storage_AudioRecordTypeDef, StreamSeq);
PLAYBACK_TASK_ASSERT_FIELD(Playback_MachineRecordTypeDef, Storage_AudioRecordTypeDef, OpenResult);
PLAYBACK_TASK_ASSERT_FIELD(Playback_MachineRecordTypeDef, Storage_AudioRecordTypeDef, Generation);
PLAYBACK_TASK_ASSERT_FIELD(Playback_MachineRecordTypeDef, Storage_AudioRecordTypeDef, TrackCount);
PLAYBACK_TASK_ASSERT_FIELD(Playback_MachineRecordTypeDef, Storage_AudioRecordTypeDef, AudioBytes);
PLAYBACK_TASK_ASSERT_FIELD(Playback_MachineRecordTypeDef, Storage_AudioRecordTypeDef, End);

_Static_assert(sizeof(Playback_MachineOpenResultTypeDef) == sizeof(Storage_AudioOpenResultTypeDef),
               "open result enums must have the same width");
_Static_assert((int)PLAYBACK_MACHINE_OPEN_PENDING == (int)STORAGE_AUDIO_OPEN_PENDING, "open result PENDING differs");
_Static_assert((int)PLAYBACK_MACHINE_OPEN_OK == (int)STORAGE_AUDIO_OPEN_OK, "open result OK differs");
_Static_assert((int)PLAYBACK_MACHINE_OPEN_NOT_READY == (int)STORAGE_AUDIO_OPEN_NOT_READY, "open result NOT_READY differs");
_Static_assert((int)PLAYBACK_MACHINE_OPEN_STALE == (int)STORAGE_AUDIO_OPEN_STALE, "open result STALE differs");
_Static_assert((int)PLAYBACK_MACHINE_OPEN_NO_TRACK == (int)STORAGE_AUDIO_OPEN_NO_TRACK, "open result NO_TRACK differs");
_Static_assert((int)PLAYBACK_MACHINE_OPEN_BAD_FILE == (int)STORAGE_AUDIO_OPEN_BAD_FILE, "open result BAD_FILE differs");
_Static_assert((int)PLAYBACK_MACHINE_OPEN_IO_ERROR == (int)STORAGE_AUDIO_OPEN_IO_ERROR, "open result IO_ERROR differs");

_Static_assert(sizeof(Playback_MachineEndTypeDef) == sizeof(Storage_AudioEndTypeDef),
               "end enums must have the same width");
_Static_assert((int)PLAYBACK_MACHINE_END_NONE == (int)STORAGE_AUDIO_END_NONE, "end NONE differs");
_Static_assert((int)PLAYBACK_MACHINE_END_ENDED == (int)STORAGE_AUDIO_END_ENDED, "end ENDED differs");
_Static_assert((int)PLAYBACK_MACHINE_END_READ_ERROR == (int)STORAGE_AUDIO_END_READ_ERROR, "end READ_ERROR differs");
_Static_assert((int)PLAYBACK_MACHINE_END_CARD_REMOVED == (int)STORAGE_AUDIO_END_CARD_REMOVED, "end CARD_REMOVED differs");

static const char playback_task_log_tag[] = "PLAYBACK";

/* 命令 FIFO（GUI → 播放任务）与状态信箱（播放任务 → GUI，长度 1 覆盖写、窥视读）：静态创建，DTCM .bss。 */
static uint8_t playback_task_command_storage[PLAYBACK_COMMAND_QUEUE_DEPTH * sizeof(Playback_CommandTypeDef)];
static uint8_t playback_task_snapshot_storage[sizeof(Playback_SnapshotTypeDef)];
static StaticQueue_t playback_task_command_control;
static StaticQueue_t playback_task_snapshot_control;
static QueueHandle_t playback_task_command_queue;
static QueueHandle_t playback_task_snapshot_queue;

/* 由 playback_task() 在队列创建之后发布；回调与 playback_command_send() 只在它非空时才发通知。 */
static volatile TaskHandle_t playback_task_handle;

/* 仅播放任务访问。 */
static Playback_MachineTypeDef playback_task_machine;

/**
 * @brief 取单调毫秒时刻（B9）。
 * @return 由 tick 换算的毫秒，32 位回绕与 tick 一致（约 49.7 天）。
 */
static uint32_t playback_task_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/**
 * @brief 向播放任务事件槽置 FLAG（任务上下文）。
 * @param[in] flag 要置的 FLAG。
 * @note  任务句柄尚未发布时静默忽略：此时播放任务还没启动，没有等待者。
 */
static void playback_task_notify(uint32_t flag)
{
    TaskHandle_t task = playback_task_handle;

    if (task != NULL)
    {
        (void)xTaskNotifyIndexed(task, (UBaseType_t)PLAYBACK_NOTIFY_EVENT, flag, eSetBits);
    }
}

/**
 * @brief 向播放任务发命令（公开合同，GUI 音乐分区调用）。
 * @param[in] command 命令；内容被拷入 FIFO。
 * @return PLAYBACK_OK 已入队并置命令 FLAG；FIFO 已满、参数为空或播放任务尚未启动为 PLAYBACK_QUEUE_FULL。
 * @note  任务上下文调用，不阻塞（超时 0），不得在 ISR 调用。
 */
Playback_StatusTypeDef playback_command_send(const Playback_CommandTypeDef *command)
{
    if ((command == NULL) || (playback_task_command_queue == NULL) || (playback_task_handle == NULL))
    {
        return PLAYBACK_QUEUE_FULL;
    }

    if (xQueueSend(playback_task_command_queue, command, 0U) != pdTRUE)
    {
        return PLAYBACK_QUEUE_FULL;
    }

    playback_task_notify(PLAYBACK_NOTIFY_FLAG_COMMAND);
    return PLAYBACK_OK;
}

/**
 * @brief 拷出最新一份完整快照，不取走（公开合同，GUI 音乐分区调用）。
 * @param[out] snapshot 接收快照；播放任务启动前或首次发布前为全零（停止、无原因、无当前曲）。
 * @note  任务上下文调用，不阻塞；整份原子可见。
 */
void playback_snapshot_read(Playback_SnapshotTypeDef *snapshot)
{
    if (snapshot == NULL)
    {
        return;
    }

    if ((playback_task_snapshot_queue == NULL) ||
        (xQueuePeek(playback_task_snapshot_queue, snapshot, 0U) != pdTRUE))
    {
        (void)memset(snapshot, 0, sizeof(*snapshot));
    }
}

/**
 * @brief 状态机发布快照：整份覆盖状态信箱，不唤醒 GUI（K2）。
 * @param[in] snapshot 要发布的快照。
 */
static void playback_task_publish_snapshot(const Playback_SnapshotTypeDef *snapshot)
{
    (void)xQueueOverwrite(playback_task_snapshot_queue, snapshot);
}

/**
 * @brief 把状态机的请求写进 Storage 请求信箱。
 * @param[in] request 状态机请求；StreamSeq 为 0 表示无流。
 * @return Storage 已接收返回 true；信箱尚未初始化返回 false。
 */
static bool playback_task_write_request(const Playback_MachineRequestTypeDef *request)
{
    Storage_AudioRequestTypeDef storage_request;

    storage_request.StreamSeq = request->StreamSeq;
    storage_request.Generation = request->Generation;
    storage_request.Position = request->Position;
    return storage_audio_stream_request(&storage_request);
}

/**
 * @brief 窥视 Storage 流记录信箱，不消费。
 * @param[out] record 接收状态机视图的记录。
 * @return 信箱里已有记录返回 true。
 */
static bool playback_task_read_record(Playback_MachineRecordTypeDef *record)
{
    Storage_AudioRecordTypeDef storage_record;

    if (!storage_audio_stream_get_record(&storage_record))
    {
        return false;
    }

    record->StreamSeq = storage_record.StreamSeq;
    record->OpenResult = (Playback_MachineOpenResultTypeDef)storage_record.OpenResult;
    record->Generation = storage_record.Generation;
    record->TrackCount = storage_record.TrackCount;
    record->AudioBytes = storage_record.AudioBytes;
    record->End = (Playback_MachineEndTypeDef)storage_record.End;
    return true;
}

/**
 * @brief 把 Storage 块转成状态机的块视图。
 * @param[in] storage_block Storage 块。
 * @param[out] block 块视图；Handle 是块指针，数据直接指向块内容。
 */
static void playback_task_fill_block(Storage_AudioBlockTypeDef *storage_block, Playback_MachineBlockTypeDef *block)
{
    block->Handle = storage_block;
    block->StreamSeq = storage_block->StreamSeq;
    block->ValidBytes = storage_block->ValidBytes;
    block->Data = storage_block->Data;
}

/**
 * @brief 窥视就绪队列队首，不取走。
 * @param[out] block 接收块视图。
 * @return 队列非空返回 true。
 */
static bool playback_task_peek_ready_block(Playback_MachineBlockTypeDef *block)
{
    Storage_AudioBlockTypeDef *storage_block = storage_audio_block_peek_ready();

    if (storage_block == NULL)
    {
        return false;
    }
    playback_task_fill_block(storage_block, block);
    return true;
}

/**
 * @brief 取走就绪队列队首。
 * @param[out] block 接收块视图；块归播放任务持有，直到 playback_task_return_block()。
 * @return 队列非空返回 true。
 */
static bool playback_task_take_ready_block(Playback_MachineBlockTypeDef *block)
{
    Storage_AudioBlockTypeDef *storage_block = storage_audio_block_take_ready();

    if (storage_block == NULL)
    {
        return false;
    }
    playback_task_fill_block(storage_block, block);
    return true;
}

/**
 * @brief 把块放回空闲队列并置 Storage 的“块已归还”FLAG。
 * @param[in] block 状态机持有的块视图。
 */
static void playback_task_return_block(const Playback_MachineBlockTypeDef *block)
{
    (void)storage_audio_stream_return_block((Storage_AudioBlockTypeDef *)block->Handle);
}

/**
 * @brief 状态机的 INFO 日志出口：非阻塞投递到 Log Task，失败（队列满）丢弃。
 * @param[in] text 日志文本，由 Service_Log 复制。
 */
static void playback_task_log_info(const char *text)
{
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, playback_task_log_tag, text);
}

/** @brief 状态机的全部外部依赖：存储窗口经本文件适配，播放模块直接填 Service_Playback_* 函数指针。 */
static const Playback_MachineOpsTypeDef playback_task_ops = {
    .Storage = {
        .WriteRequest = playback_task_write_request,
        .ReadRecord = playback_task_read_record,
        .PeekReadyBlock = playback_task_peek_ready_block,
        .TakeReadyBlock = playback_task_take_ready_block,
        .ReturnBlock = playback_task_return_block,
    },
    .Player = {
        .BeginStream = Service_Playback_BeginStream,
        .Feed = Service_Playback_Feed,
        .FinishInput = Service_Playback_FinishInput,
        .Pump = Service_Playback_Pump,
        .PauseOutput = Service_Playback_PauseOutput,
        .ResumeOutput = Service_Playback_ResumeOutput,
        .AbandonStream = Service_Playback_AbandonStream,
    },
    .PublishSnapshot = playback_task_publish_snapshot,
    .LogInfo = playback_task_log_info,
};

/**
 * @brief Storage 音频流的唤醒回调：把原因映射成本任务的 FLAG。
 * @param[in] reason 唤醒原因。
 * @note  在 Storage Task 上下文调用，不得阻塞。
 */
static void playback_task_storage_wake(Storage_AudioWakeTypeDef reason)
{
    playback_task_notify((reason == STORAGE_AUDIO_WAKE_STREAM_RECORD) ? PLAYBACK_NOTIFY_FLAG_STREAM_RECORD
                                                                      : PLAYBACK_NOTIFY_FLAG_BLOCK_READY);
}

/**
 * @brief Platform 音频事件回调：只置音频 FLAG 唤醒播放任务（ARC-10）。
 * @param[in] context 注册时传入的上下文，未使用。
 * @note  在 ISR 中调用，只用 FromISR 通知；半区计数、欠载与故障由任务经 Platform_Audio_GetStatus() 读取，
 *        所以通知被合并或迟到都不丢失事实。I2S2 DMA 的 NVIC 优先级须在 FromISR 可调范围（E10）。
 */
static void playback_task_audio_event(void *context)
{
    TaskHandle_t task = playback_task_handle;
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)context;
    if (task != NULL)
    {
        (void)xTaskNotifyIndexedFromISR(task,
                                        (UBaseType_t)PLAYBACK_NOTIFY_EVENT,
                                        PLAYBACK_NOTIFY_FLAG_AUDIO,
                                        eSetBits,
                                        &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/**
 * @brief 创建命令 FIFO 与状态信箱（静态）。
 * @return 两个队列都创建成功返回 true。
 */
static bool playback_task_create_queues(void)
{
    playback_task_command_queue = xQueueCreateStatic(PLAYBACK_COMMAND_QUEUE_DEPTH,
                                                     sizeof(Playback_CommandTypeDef),
                                                     playback_task_command_storage,
                                                     &playback_task_command_control);
    playback_task_snapshot_queue = xQueueCreateStatic(1U,
                                                      sizeof(Playback_SnapshotTypeDef),
                                                      playback_task_snapshot_storage,
                                                      &playback_task_snapshot_control);
    return (playback_task_command_queue != NULL) && (playback_task_snapshot_queue != NULL);
}

/**
 * @brief 有界等待事件槽，成功时一次收取并清除全部事件 FLAG。
 * @param[in] ticks 最长阻塞时间。
 * @note  FLAG 只用于唤醒；超时也算一次唤醒，让按时刻到期的判定得到评估。
 */
static void playback_task_wait_event(TickType_t ticks)
{
    uint32_t flags = 0U;

    (void)xTaskNotifyWaitIndexed((UBaseType_t)PLAYBACK_NOTIFY_EVENT, 0U, PLAYBACK_NOTIFY_FLAG_EVENT_MASK, &flags, ticks);
}

/**
 * @brief 把命令 FIFO 取空（≤ 深度条），按入队顺序放进 commands。
 * @param[out] commands 至少容纳 PLAYBACK_COMMAND_QUEUE_DEPTH 条。
 * @return 取出的命令条数。
 * @note  不按唤醒次数计数：醒来一次就取空；取空之后新入队的命令会再置 FLAG。
 */
static uint32_t playback_task_drain_commands(Playback_CommandTypeDef *commands)
{
    uint32_t count = 0U;

    while ((count < PLAYBACK_COMMAND_QUEUE_DEPTH) &&
           (xQueueReceive(playback_task_command_queue, &commands[count], 0U) == pdTRUE))
    {
        count++;
    }
    return count;
}

/**
 * @brief 播放任务入口：初始化后按事件唤醒，每次唤醒调用一次状态机（B1）。
 * @param[in] handle 当前未使用，保留为 FreeRTOS TaskFunction_t 规定的参数。
 * @note  初始化顺序：命令 FIFO 与状态信箱 → 播放模块 → 注册 Storage 与 Platform 的唤醒回调 → 发布任务句柄。
 *        任一步失败则记日志并删除本任务，快照保持全零；任务句柄未发布（或已撤回）时 playback_command_send()
 *        返回 PLAYBACK_QUEUE_FULL。须在 storage_task() 完成 storage_audio_stream_init() 之后收到第一条命令
 *        （app_tasks.c 先于本任务创建 Storage Task，它在首次阻塞前即完成该初始化）。
 */
void playback_task(void *handle)
{
    Playback_CommandTypeDef commands[PLAYBACK_COMMAND_QUEUE_DEPTH];
    uint32_t command_count;

    (void)handle;

    if (!playback_task_create_queues())
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, playback_task_log_tag, "Queue creation failed.");
        vTaskDelete(NULL);
    }

    if (Service_Playback_Init() != SERVICE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, playback_task_log_tag, "Playback service init failed.");
        vTaskDelete(NULL);
    }

    playback_machine_init(&playback_task_machine, &playback_task_ops);
    playback_task_handle = xTaskGetCurrentTaskHandle();
    storage_audio_stream_set_wake_callback(playback_task_storage_wake);

    if (Platform_Audio_SetEventCallback(playback_task_audio_event, NULL) != PLATFORM_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, playback_task_log_tag, "Audio callback registration failed.");
        storage_audio_stream_set_wake_callback(NULL);
        playback_task_handle = NULL;
        vTaskDelete(NULL);
    }

    for (;;)
    {
        playback_task_wait_event(pdMS_TO_TICKS(PLAYBACK_TASK_WAKE_PERIOD_MS));
        command_count = playback_task_drain_commands(commands);
        playback_machine_step(&playback_task_machine, commands, command_count, playback_task_now_ms());
    }
}
