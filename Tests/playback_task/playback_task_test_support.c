/**
 ******************************************************************************
 * @file    playback_task_test_support.c
 * @brief   playback_machine 主机测试的替身与场景助手实现。
 ******************************************************************************
 */

#include "playback_task_test_support.h"

#include <assert.h>
#include <string.h>

#define CALL_LOG_CAPACITY 512U
#define FAKE_PREFILL_FRAMES 2U   /* 两个半区的预填折合的帧数；暂停且已满时不再解码。 */

/* ---------------------------------------------------------------------------
 * 调用日志与快照记录
 * ------------------------------------------------------------------------- */

static CallKindTypeDef call_log[CALL_LOG_CAPACITY];
static uint32_t call_log_length;
static uint32_t publish_count;
static Playback_SnapshotTypeDef publish_last;

/**
 * @brief 追加一条调用记录。
 * @param[in] kind 调用种类。
 */
static void call_log_add(CallKindTypeDef kind)
{
    assert(call_log_length < CALL_LOG_CAPACITY);
    call_log[call_log_length] = kind;
    call_log_length++;
}

/**
 * @brief 清空调用日志与快照记录。
 */
void call_log_reset(void)
{
    call_log_length = 0U;
    publish_count = 0U;
    memset(&publish_last, 0, sizeof(publish_last));
}

/**
 * @brief 日志条数。
 * @return 条数。
 */
uint32_t call_log_count(void)
{
    return call_log_length;
}

/**
 * @brief 取第 index 条记录。
 * @param[in] index 位置。
 * @return 调用种类。
 */
CallKindTypeDef call_log_at(uint32_t index)
{
    assert(index < call_log_length);
    return call_log[index];
}

/**
 * @brief 统计某种调用的次数。
 * @param[in] kind 调用种类。
 * @return 次数。
 */
uint32_t call_log_count_of(CallKindTypeDef kind)
{
    uint32_t count = 0U;

    for (uint32_t i = 0U; i < call_log_length; i++)
    {
        if (call_log[i] == kind)
        {
            count++;
        }
    }
    return count;
}

/**
 * @brief 找某种调用第 nth 次出现的位置。
 * @param[in] kind 调用种类。
 * @param[in] nth 从 0 起的次序。
 * @return 位置；不存在返回 UINT32_MAX。
 */
uint32_t call_log_index_of(CallKindTypeDef kind, uint32_t nth)
{
    for (uint32_t i = 0U; i < call_log_length; i++)
    {
        if (call_log[i] == kind)
        {
            if (nth == 0U)
            {
                return i;
            }
            nth--;
        }
    }
    return UINT32_MAX;
}

/**
 * @brief PublishSnapshot 替身：记录最新快照与发布次数。
 * @param[in] snapshot 被发布的快照。
 */
static void fake_publish(const Playback_SnapshotTypeDef *snapshot)
{
    call_log_add(CALL_PUBLISH);
    publish_last = *snapshot;
    publish_count++;
}

/**
 * @brief 发布次数。
 * @return 次数。
 */
uint32_t published_count(void)
{
    return publish_count;
}

/**
 * @brief 最近一次发布的快照；未发布过为全零。
 * @return 快照副本。
 */
Playback_SnapshotTypeDef published_last(void)
{
    return publish_last;
}

/* ---------------------------------------------------------------------------
 * 假存储窗口
 * ------------------------------------------------------------------------- */

typedef struct
{
    uint32_t StreamSeq;
    uint32_t ValidBytes;
    uint8_t Data[FAKE_BLOCK_CAPACITY];
    bool Queued;
    bool Taken;
} FakeBlockTypeDef;

static FakeBlockTypeDef fake_blocks[FAKE_BLOCK_POOL];
static uint32_t fake_ready_order[FAKE_BLOCK_POOL];   /* 就绪队列，存 fake_blocks 下标。 */
static uint32_t fake_ready_length;
static uint32_t fake_request_count;
static Playback_MachineRequestTypeDef fake_last_request;
static bool fake_record_present;
static Playback_MachineRecordTypeDef fake_record;
static uint32_t fake_outstanding;
static uint32_t fake_max_outstanding;
static uint32_t fake_returned;
static uint32_t fake_returned_seq_log[64];

/**
 * @brief 复位假存储窗口。
 */
static void fake_storage_reset(void)
{
    memset(fake_blocks, 0, sizeof(fake_blocks));
    fake_ready_length = 0U;
    fake_request_count = 0U;
    memset(&fake_last_request, 0, sizeof(fake_last_request));
    fake_record_present = false;
    memset(&fake_record, 0, sizeof(fake_record));
    fake_outstanding = 0U;
    fake_max_outstanding = 0U;
    fake_returned = 0U;
    memset(fake_returned_seq_log, 0, sizeof(fake_returned_seq_log));
}

/**
 * @brief WriteRequest 替身：覆盖写请求信箱。
 * @param[in] request 请求。
 * @return 总是 true。
 */
static bool fake_write_request(const Playback_MachineRequestTypeDef *request)
{
    call_log_add(CALL_WRITE_REQUEST);
    fake_last_request = *request;
    fake_request_count++;
    return true;
}

/**
 * @brief ReadRecord 替身：窥视流记录信箱。
 * @param[out] record 接收记录。
 * @return 信箱里有记录为 true。
 */
static bool fake_read_record(Playback_MachineRecordTypeDef *record)
{
    if (!fake_record_present)
    {
        return false;
    }
    *record = fake_record;
    return true;
}

/**
 * @brief 把就绪队列队首填成块视图。
 * @param[out] block 接收视图。
 * @return 队列非空为 true。
 */
static bool fake_view_head(Playback_MachineBlockTypeDef *block)
{
    if (fake_ready_length == 0U)
    {
        return false;
    }
    FakeBlockTypeDef *head = &fake_blocks[fake_ready_order[0]];
    block->Handle = head;
    block->StreamSeq = head->StreamSeq;
    block->ValidBytes = head->ValidBytes;
    block->Data = head->Data;
    return true;
}

/**
 * @brief PeekReadyBlock 替身：窥视队首。
 * @param[out] block 接收视图。
 * @return 队列非空为 true。
 */
static bool fake_peek_ready_block(Playback_MachineBlockTypeDef *block)
{
    return fake_view_head(block);
}

/**
 * @brief TakeReadyBlock 替身：取走队首，并统计同时持有的块数。
 * @param[out] block 接收视图。
 * @return 队列非空为 true。
 */
static bool fake_take_ready_block(Playback_MachineBlockTypeDef *block)
{
    if (!fake_view_head(block))
    {
        return false;
    }
    call_log_add(CALL_TAKE_BLOCK);
    FakeBlockTypeDef *head = &fake_blocks[fake_ready_order[0]];
    head->Queued = false;
    head->Taken = true;
    for (uint32_t i = 1U; i < fake_ready_length; i++)
    {
        fake_ready_order[i - 1U] = fake_ready_order[i];
    }
    fake_ready_length--;
    fake_outstanding++;
    if (fake_outstanding > fake_max_outstanding)
    {
        fake_max_outstanding = fake_outstanding;
    }
    return true;
}

/**
 * @brief ReturnBlock 替身：归还一块；重复归还或归还未取走的块视为缺陷。
 * @param[in] block 归还的块。
 */
static void fake_return_block(const Playback_MachineBlockTypeDef *block)
{
    FakeBlockTypeDef *fake = (FakeBlockTypeDef *)block->Handle;

    call_log_add(CALL_RETURN_BLOCK);
    assert(fake->Taken);
    fake->Taken = false;
    assert(fake_outstanding > 0U);
    fake_outstanding--;
    if (fake_returned < 64U)
    {
        fake_returned_seq_log[fake_returned] = fake->StreamSeq;
    }
    fake_returned++;
}

/**
 * @brief 已写请求次数。
 * @return 次数。
 */
uint32_t fake_storage_request_count(void)
{
    return fake_request_count;
}

/**
 * @brief 最近一次写入的请求。
 * @return 请求副本。
 */
Playback_MachineRequestTypeDef fake_storage_last_request(void)
{
    return fake_last_request;
}

/**
 * @brief 写流记录信箱：开启 OK。
 * @param[in] stream_seq 流序号。
 * @param[in] generation 实际代次。
 * @param[in] track_count 曲目数。
 * @param[in] audio_bytes 音频字节数。
 */
void fake_storage_open_ok(uint32_t stream_seq, uint32_t generation, uint16_t track_count, uint32_t audio_bytes)
{
    Playback_MachineRecordTypeDef record;

    memset(&record, 0, sizeof(record));
    record.StreamSeq = stream_seq;
    record.OpenResult = PLAYBACK_MACHINE_OPEN_OK;
    record.Generation = generation;
    record.TrackCount = track_count;
    record.AudioBytes = audio_bytes;
    fake_storage_write_record(&record);
}

/**
 * @brief 整份覆盖写流记录信箱。
 * @param[in] record 记录。
 */
void fake_storage_write_record(const Playback_MachineRecordTypeDef *record)
{
    fake_record = *record;
    fake_record_present = true;
}

/**
 * @brief 保留开启参数，只改终止状态。
 * @param[in] stream_seq 流序号；与现有记录不同时整份重写为只含终止状态的记录。
 * @param[in] end 终止状态。
 */
void fake_storage_set_end(uint32_t stream_seq, Playback_MachineEndTypeDef end)
{
    if (!fake_record_present || (fake_record.StreamSeq != stream_seq))
    {
        memset(&fake_record, 0, sizeof(fake_record));
        fake_record.StreamSeq = stream_seq;
        fake_record_present = true;
    }
    fake_record.End = end;
}

/**
 * @brief 向就绪队列放一块。
 * @param[in] stream_seq 块头流序号。
 * @param[in] length 有效字节数，1..FAKE_BLOCK_CAPACITY。
 * @param[in] fill_value 第一个字节的值，其后逐字节加一。
 */
void fake_storage_enqueue_block(uint32_t stream_seq, uint32_t length, uint8_t fill_value)
{
    assert((length >= 1U) && (length <= FAKE_BLOCK_CAPACITY));
    for (uint32_t i = 0U; i < FAKE_BLOCK_POOL; i++)
    {
        FakeBlockTypeDef *fake = &fake_blocks[i];

        if (!fake->Queued && !fake->Taken)
        {
            fake->StreamSeq = stream_seq;
            fake->ValidBytes = length;
            for (uint32_t j = 0U; j < length; j++)
            {
                fake->Data[j] = (uint8_t)(fill_value + j);
            }
            fake->Queued = true;
            fake_ready_order[fake_ready_length] = i;
            fake_ready_length++;
            return;
        }
    }
    assert(false && "假块池用尽");
}

/**
 * @brief 就绪队列长度。
 * @return 块数。
 */
uint32_t fake_storage_ready_count(void)
{
    return fake_ready_length;
}

/**
 * @brief 当前被取走未归还的块数。
 * @return 块数。
 */
uint32_t fake_storage_outstanding(void)
{
    return fake_outstanding;
}

/**
 * @brief 历史上同时持有块数的最大值。
 * @return 块数。
 */
uint32_t fake_storage_max_outstanding(void)
{
    return fake_max_outstanding;
}

/**
 * @brief 已归还块数。
 * @return 块数。
 */
uint32_t fake_storage_returned_count(void)
{
    return fake_returned;
}

/**
 * @brief 第 index 个被归还块的流序号。
 * @param[in] index 归还次序。
 * @return 流序号。
 */
uint32_t fake_storage_returned_seq(uint32_t index)
{
    assert((index < fake_returned) && (index < 64U));
    return fake_returned_seq_log[index];
}

/* ---------------------------------------------------------------------------
 * 假播放模块
 * ------------------------------------------------------------------------- */

static FakePlayerConfigTypeDef player_config;
static bool player_active;
static bool player_paused;
static bool player_input_finished;
static bool player_first_frame_done;
static bool player_tail_released;
static uint32_t player_window_fill;
static uint32_t player_decoded_ms;
static uint32_t player_frames_since_begin;
static uint32_t player_begin_count;
static uint32_t player_last_audio_bytes;
static uint32_t player_fed_total;
static uint8_t player_fed[FAKE_FED_CAPACITY];

/**
 * @brief 把假播放模块恢复为默认配置与空状态。
 */
static void fake_player_reset(void)
{
    player_config.WindowCapacity = 40U;
    player_config.BytesPerFrame = 10U;
    player_config.FrameMs = 26U;
    player_config.DurationMs = 180000U;
    player_config.FramesPerPump = 2U;
    player_active = false;
    player_paused = false;
    player_input_finished = false;
    player_first_frame_done = false;
    player_tail_released = false;
    player_window_fill = 0U;
    player_decoded_ms = 0U;
    player_frames_since_begin = 0U;
    player_begin_count = 0U;
    player_last_audio_bytes = 0U;
    player_fed_total = 0U;
    memset(player_fed, 0, sizeof(player_fed));
}

/**
 * @brief 改写假播放模块的建模参数；应在场景开始前调用。
 * @param[in] config 新参数。
 */
void fake_player_configure(const FakePlayerConfigTypeDef *config)
{
    player_config = *config;
}

/**
 * @brief BeginStream 替身：重置解码器、窗口与已解码时间；对暂停也复位，与真实模块一致。
 * @param[in] audio_bytes 音频区间字节数。
 * @return SERVICE_OK。
 */
static Service_StatusTypeDef fake_begin_stream(uint32_t audio_bytes)
{
    call_log_add(CALL_BEGIN_STREAM);
    player_active = true;
    player_paused = false;
    player_input_finished = false;
    player_first_frame_done = false;
    player_tail_released = false;
    player_window_fill = 0U;
    player_decoded_ms = 0U;
    player_frames_since_begin = 0U;
    player_begin_count++;
    player_last_audio_bytes = audio_bytes;
    return SERVICE_OK;
}

/**
 * @brief Feed 替身：按窗口剩余容量接收字节。
 * @param[in] data 字节。
 * @param[in] length 字节数。
 * @param[out] accepted 实际接收数。
 * @return SERVICE_OK；无活动流返回 SERVICE_NOT_READY。
 */
static Service_StatusTypeDef fake_feed(const uint8_t *data, uint32_t length, uint32_t *accepted)
{
    call_log_add(CALL_FEED);
    if (!player_active)
    {
        *accepted = 0U;
        return SERVICE_NOT_READY;
    }

    uint32_t room = player_config.WindowCapacity - player_window_fill;
    uint32_t take = (length < room) ? length : room;

    if (player_input_finished)
    {
        take = 0U;
    }
    for (uint32_t i = 0U; i < take; i++)
    {
        if (player_fed_total + i < FAKE_FED_CAPACITY)
        {
            player_fed[player_fed_total + i] = data[i];
        }
    }
    player_fed_total += take;
    player_window_fill += take;
    *accepted = take;
    return SERVICE_OK;
}

/**
 * @brief FinishInput 替身：标记输入读尽。
 * @return SERVICE_OK。
 */
static Service_StatusTypeDef fake_finish_input(void)
{
    call_log_add(CALL_FINISH_INPUT);
    player_input_finished = true;
    return SERVICE_OK;
}

/**
 * @brief Pump 替身：窗口够一帧就解一帧（每次至多 FramesPerPump 帧）；不够且输入未结束回报 NeedInput；
 *        输入读尽、窗口空且 fake_player_release_tail() 之后回报 StreamEnded。
 * @param[out] report 本次推进的事实。
 * @return SERVICE_OK；无活动流返回 SERVICE_NOT_READY。
 */
static Service_StatusTypeDef fake_pump(Service_Playback_PumpReportTypeDef *report)
{
    call_log_add(CALL_PUMP);
    memset(report, 0, sizeof(*report));
    if (!player_active)
    {
        return SERVICE_NOT_READY;
    }

    for (uint32_t frames = 0U; frames < player_config.FramesPerPump; frames++)
    {
        if (player_paused && (player_frames_since_begin >= FAKE_PREFILL_FRAMES))
        {
            break;   /* 暂停且预填已满：输出缓冲是满的，不再解码。 */
        }
        if (player_window_fill >= player_config.BytesPerFrame)
        {
            player_window_fill -= player_config.BytesPerFrame;
            player_decoded_ms += player_config.FrameMs;
            player_frames_since_begin++;
            if (!player_first_frame_done)
            {
                player_first_frame_done = true;
                report->FirstFrame = true;
            }
        }
        else if (player_input_finished)
        {
            player_window_fill = 0U;   /* 截断尾部残余静默丢弃。 */
            break;
        }
        else
        {
            report->NeedInput = true;
            break;
        }
    }

    if (player_first_frame_done)
    {
        report->StreamInfo.SampleRateHz = 44100U;
        report->StreamInfo.Channels = 2U;
        report->StreamInfo.TotalDurationMs = player_config.DurationMs;
        report->StreamInfo.DurationFromHeader = true;
    }
    report->DecodedMs = player_decoded_ms;
    if (player_input_finished && (player_window_fill == 0U) && !player_paused && player_tail_released)
    {
        report->StreamEnded = true;
    }
    return SERVICE_OK;
}

/**
 * @brief PauseOutput 替身。
 * @return 有活动流 SERVICE_OK，否则 SERVICE_NOT_READY。
 */
static Service_StatusTypeDef fake_pause_output(void)
{
    call_log_add(CALL_PAUSE);
    if (!player_active)
    {
        return SERVICE_NOT_READY;
    }
    player_paused = true;
    return SERVICE_OK;
}

/**
 * @brief ResumeOutput 替身。
 * @return 有活动流 SERVICE_OK，否则 SERVICE_NOT_READY。
 */
static Service_StatusTypeDef fake_resume_output(void)
{
    call_log_add(CALL_RESUME);
    if (!player_active)
    {
        return SERVICE_NOT_READY;
    }
    player_paused = false;
    return SERVICE_OK;
}

/**
 * @brief AbandonStream 替身：使流失效。
 * @return SERVICE_OK。
 */
static Service_StatusTypeDef fake_abandon_stream(void)
{
    call_log_add(CALL_ABANDON);
    player_active = false;
    player_paused = false;
    return SERVICE_OK;
}

/**
 * @brief BeginStream 被调用的次数。
 * @return 次数。
 */
uint32_t fake_player_begin_count(void)
{
    return player_begin_count;
}

/**
 * @brief 最近一次 BeginStream 带的音频字节数。
 * @return 字节数。
 */
uint32_t fake_player_last_audio_bytes(void)
{
    return player_last_audio_bytes;
}

/**
 * @brief 累计喂入的字节数。
 * @return 字节数。
 */
uint32_t fake_player_fed_total(void)
{
    return player_fed_total;
}

/**
 * @brief 已喂入的字节内容。
 * @return 指向缓冲的指针。
 */
const uint8_t *fake_player_fed_bytes(void)
{
    return player_fed;
}

/**
 * @brief 输出是否处于暂停。
 * @return 暂停为 true。
 */
bool fake_player_is_paused(void)
{
    return player_paused;
}

/**
 * @brief 是否有活动流。
 * @return 有为 true。
 */
bool fake_player_stream_active(void)
{
    return player_active;
}

/**
 * @brief 是否已收到 FinishInput。
 * @return 是为 true。
 */
bool fake_player_input_finished(void)
{
    return player_input_finished;
}

/**
 * @brief 模拟最后一个含真实数据的半区已释放。
 */
void fake_player_release_tail(void)
{
    player_tail_released = true;
}

/* ---------------------------------------------------------------------------
 * 场景助手
 * ------------------------------------------------------------------------- */

static Playback_MachineTypeDef world_machine_instance;
static Playback_MachineOpsTypeDef world_ops;
static uint32_t world_now_ms;
static uint32_t world_command_seq;

/**
 * @brief 复位全部替身并重新初始化状态机。
 */
void world_reset(void)
{
    call_log_reset();
    fake_storage_reset();
    fake_player_reset();
    world_now_ms = 1000U;
    world_command_seq = 0U;

    world_ops.Storage.WriteRequest = fake_write_request;
    world_ops.Storage.ReadRecord = fake_read_record;
    world_ops.Storage.PeekReadyBlock = fake_peek_ready_block;
    world_ops.Storage.TakeReadyBlock = fake_take_ready_block;
    world_ops.Storage.ReturnBlock = fake_return_block;
    world_ops.Player.BeginStream = fake_begin_stream;
    world_ops.Player.Feed = fake_feed;
    world_ops.Player.FinishInput = fake_finish_input;
    world_ops.Player.Pump = fake_pump;
    world_ops.Player.PauseOutput = fake_pause_output;
    world_ops.Player.ResumeOutput = fake_resume_output;
    world_ops.Player.AbandonStream = fake_abandon_stream;
    world_ops.PublishSnapshot = fake_publish;
    playback_machine_init(&world_machine_instance, &world_ops);
}

/**
 * @brief 状态机实例。
 * @return 实例指针。
 */
Playback_MachineTypeDef *world_machine(void)
{
    return &world_machine_instance;
}

/**
 * @brief 执行一次带命令的唤醒，时刻递增 10 ms。
 * @param[in] commands 命令数组。
 * @param[in] count 命令条数。
 */
void world_step_commands(const Playback_CommandTypeDef *commands, uint32_t count)
{
    world_now_ms += 10U;
    playback_machine_step(&world_machine_instance, commands, count, world_now_ms);
}

/**
 * @brief 发一条命令（序号自增）并唤醒一次。
 * @param[in] kind 命令种类（SELECT 请用 world_send_select）。
 */
void world_send(Playback_CommandKindTypeDef kind)
{
    Playback_CommandTypeDef command;

    memset(&command, 0, sizeof(command));
    command.Kind = kind;
    world_command_seq++;
    command.Seq = world_command_seq;
    world_step_commands(&command, 1U);
}

/**
 * @brief 发一条 SELECT（序号自增）并唤醒一次。
 * @param[in] generation 列表窗口代次。
 * @param[in] index 播放列表位置。
 */
void world_send_select(uint32_t generation, uint16_t index)
{
    Playback_CommandTypeDef command;

    memset(&command, 0, sizeof(command));
    command.Kind = PLAYBACK_COMMAND_SELECT;
    world_command_seq++;
    command.Seq = world_command_seq;
    command.Generation = generation;
    command.Index = index;
    world_step_commands(&command, 1U);
}

/**
 * @brief 没有命令的一次唤醒。
 */
void world_wake(void)
{
    world_step_commands(NULL, 0U);
}

/**
 * @brief 最近一次写入请求信箱的流序号。
 * @return 流序号。
 */
uint32_t world_current_seq(void)
{
    return fake_last_request.StreamSeq;
}

/**
 * @brief GUI 此刻读到的快照，即最近一次发布到状态信箱的内容；未发布过为全零。
 * @return 快照副本。
 */
Playback_SnapshotTypeDef world_snapshot(void)
{
    return publish_last;
}

/**
 * @brief 已发出命令的最后序号。
 * @return 序号。
 */
uint32_t world_last_command_seq(void)
{
    return world_command_seq;
}

/**
 * @brief Storage 对当前流应答开启 OK，并唤醒一次。
 * @param[in] generation 应答带回的代次。
 * @param[in] track_count 应答带回的曲目数。
 */
void world_open_ok(uint32_t generation, uint16_t track_count)
{
    fake_storage_open_ok(world_current_seq(), generation, track_count, WORLD_AUDIO_BYTES);
    world_wake();
}

/**
 * @brief 给当前流放入一块并唤醒一次。
 * @param[in] length 块有效字节数。
 */
void world_feed_block(uint32_t length)
{
    fake_storage_enqueue_block(world_current_seq(), length, 1U);
    world_wake();
}

/**
 * @brief 从停止态 TOGGLE 开播，应答开启 OK，放入首块并唤醒，直到首帧解出。
 * @param[in] generation 开启应答的代次。
 * @param[in] track_count 开启应答的曲目数。
 */
void world_start_playing(uint32_t generation, uint16_t track_count)
{
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_open_ok(generation, track_count);
    world_feed_block(30U);
}
