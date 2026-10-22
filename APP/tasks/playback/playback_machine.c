/**
 * @file playback_machine.c
 * @brief 播放任务的纯 C 状态机：命令表、流序号、块交接、切歌清理顺序、暂停、播完自动前进与快照发布。
 * @note  一次唤醒的顺序按 #71 B1：流记录 → 归还旧序号块 → 命令 → 交字节 → 推进输出管线 → T_out、T_data 判定。
 *        B6 的结果码表在这里落地：开启结果与终止事实在 handle_record()，播放模块与 Platform 的事实在
 *        apply_pump_report()，超时在 check_timeouts()；所有停止出口都经 enter_stopped()。
 */

#include "APP/tasks/playback/playback_machine.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief 两份快照是否逐字段相等（结构体含填充，不用 memcmp）。
 * @param[in] a 快照 a。
 * @param[in] b 快照 b。
 * @return 相等为 true。
 */
static bool snapshot_equal(const Playback_SnapshotTypeDef *a, const Playback_SnapshotTypeDef *b)
{
    return (a->State == b->State) &&
           (a->StopReason == b->StopReason) &&
           (a->HasTrack == b->HasTrack) &&
           (a->Generation == b->Generation) &&
           (a->Index == b->Index) &&
           (a->PositionMs == b->PositionMs) &&
           (a->DurationMs == b->DurationMs) &&
           (a->SkippedBadCount == b->SkippedBadCount) &&
           (a->StopCount == b->StopCount) &&
           (a->HandledCommandSeq == b->HandledCommandSeq);
}

/**
 * @brief 快照与已发布的不同就整份覆盖写状态信箱（B17）。
 * @param[in,out] machine 状态机。
 */
static void publish_if_changed(Playback_MachineTypeDef *machine)
{
    if (snapshot_equal(&machine->Snapshot, &machine->Published))
    {
        return;
    }

    machine->Published = machine->Snapshot;
    if (machine->Ops->PublishSnapshot != NULL)
    {
        machine->Ops->PublishSnapshot(&machine->Published);
    }
}

/**
 * @brief 停止原因的日志名，与 Playback_StopReasonTypeDef 的枚举名（去掉前缀）一致。
 * @param[in] reason 停止原因。
 * @return 静态字符串。
 */
static const char *stop_reason_name(Playback_StopReasonTypeDef reason)
{
    static const char *const names[PLAYBACK_STOP_REASON_COUNT] = {
        "NONE", "CARD_REMOVED", "NOT_READY", "NO_TRACK", "CATALOG_CHANGED",
        "READ_ERROR", "NO_DATA", "AUDIO_ERROR", "AUDIO_FAULT", "ALL_BAD"
    };

    return ((unsigned)reason < (unsigned)PLAYBACK_STOP_REASON_COUNT) ? names[reason] : "UNKNOWN";
}

/**
 * @brief 记一条 INFO 日志：事件加曲库序号（可选）与一个可选数值，格式 "<事件> idx=<序号> [<键>=<值>]"。
 * @param[in] machine 状态机。
 * @param[in] event 事件名，静态字符串。
 * @param[in] with_index 是否附当前游标的曲库序号。
 * @note  只在命令与曲目边界调用；不记文件名（曲名属于曲名数据路径）。
 */
static void log_event(const Playback_MachineTypeDef *machine, const char *event, bool with_index)
{
    char text[48];

    if (machine->Ops->LogInfo == NULL)
    {
        return;
    }
    if (with_index)
    {
        (void)snprintf(text, sizeof(text), "%s idx=%u", event, (unsigned)machine->Snapshot.Index);
    }
    else
    {
        (void)snprintf(text, sizeof(text), "%s", event);
    }
    machine->Ops->LogInfo(text);
}

/**
 * @brief 把手中的块放回空闲队列（B15）。
 * @param[in,out] machine 状态机。
 */
static void return_held_block(Playback_MachineTypeDef *machine)
{
    if (machine->Holding)
    {
        machine->Ops->Storage.ReturnBlock(&machine->Held);
        machine->Holding = false;
        machine->HeldOffset = 0U;
    }
}

/**
 * @brief 判断播放模块对 Stop 类操作的返回是否表示 Stop 失败（Platform 已锁存 STOP_FAILED）。
 * @param[in] status 播放模块返回。
 * @return Stop 失败为 true。
 * @note  PauseOutput/AbandonStream 只在 Stop 失败时返回 SERVICE_ERROR；BeginStream 在锁存后返回
 *        SERVICE_NOT_READY，在上一条流 Stop 失败时返回 SERVICE_ERROR（Service/playback/playback_service.c）。
 */
static bool stop_failed(Service_StatusTypeDef status)
{
    return status == SERVICE_ERROR;
}

/**
 * @brief 放弃播放模块里的流（Platform Stop），输出随之停止。
 * @param[in,out] machine 状态机。
 * @return Stop 成功为 true；失败（STOP_FAILED 锁存）为 false。
 */
static bool abandon_player_stream(Playback_MachineTypeDef *machine)
{
    machine->OutputRunning = false;
    return !stop_failed(machine->Ops->Player.AbandonStream());
}

/**
 * @brief 分配下一个流序号（单调，跳过 0）。
 * @param[in,out] machine 状态机。
 * @return 新序号。
 */
static uint32_t allocate_stream_seq(Playback_MachineTypeDef *machine)
{
    machine->LastStreamSeq++;
    if (machine->LastStreamSeq == 0U)
    {
        machine->LastStreamSeq = 1U;
    }
    return machine->LastStreamSeq;
}

/**
 * @brief 新流（或“无流”）开始：清掉上一条流的记录观察与 T_data 计时。
 * @param[in,out] machine 状态机。
 * @note  T_out 的运行标志在放弃播放模块的流时清除，不在这里动。
 */
static void reset_stream_tracking(Playback_MachineTypeDef *machine)
{
    machine->SeenOpen = PLAYBACK_MACHINE_OPEN_PENDING;
    machine->SeenEnd = PLAYBACK_MACHINE_END_NONE;
    machine->DataEvent = false;
    machine->DataWaitActive = false;
    machine->PumpNeedInput = false;
}

/**
 * @brief 按游标打开一条新流：分配新序号并写请求信箱；重选同一首也是新序号（B3）。
 * @param[in,out] machine 状态机。
 * @note  不碰播放模块；旧流的清理由调用者在写请求之后做（B11）。
 */
static void open_stream_for_cursor(Playback_MachineTypeDef *machine)
{
    Playback_MachineRequestTypeDef request;

    machine->StreamSeq = allocate_stream_seq(machine);
    machine->StreamBegun = false;
    machine->InputFinished = false;
    machine->RecordValid = false;
    reset_stream_tracking(machine);

    request.StreamSeq = machine->StreamSeq;
    request.Generation = machine->Snapshot.Generation;
    request.Position = machine->Snapshot.Index;
    (void)machine->Ops->Storage.WriteRequest(&request);
}

/**
 * @brief 写“无流”请求，让 Storage 关闭文件并忽略此前的请求。
 * @param[in,out] machine 状态机。
 */
static void close_stream(Playback_MachineTypeDef *machine)
{
    Playback_MachineRequestTypeDef request;

    request.StreamSeq = 0U;
    request.Generation = 0U;
    request.Position = 0U;
    machine->StreamSeq = 0U;
    machine->StreamBegun = false;
    machine->InputFinished = false;
    machine->RecordValid = false;
    reset_stream_tracking(machine);
    (void)machine->Ops->Storage.WriteRequest(&request);
}

static void enter_stopped(Playback_MachineTypeDef *machine, Playback_StopReasonTypeDef reason, bool keep_cursor);

/**
 * @brief 切歌：按已更新的游标换到一条新流，顺序为 B11 ①–④。
 * @param[in,out] machine 状态机。
 * @note  ① 分配新序号并写请求信箱；② 放弃播放模块的流（Platform Stop，返回即交还半区）；
 *        ③ 归还手中旧块；④ 就绪队列中的旧序号块由 discard_stale_blocks() 取出即归还。
 *        位置回 0、总时长变为未知，在接受命令的同一刻生效（K4）。
 *        ② 里 Stop 失败时，已写的新流请求被“无流”覆盖，停止为 AUDIO_FAULT（B10）。
 */
static void switch_to_cursor_track(Playback_MachineTypeDef *machine)
{
    bool had_player_stream = machine->StreamBegun;
    bool stop_ok = true;

    open_stream_for_cursor(machine);
    if (had_player_stream)
    {
        stop_ok = abandon_player_stream(machine);
    }
    return_held_block(machine);
    machine->Snapshot.PositionMs = 0U;
    machine->Snapshot.DurationMs = 0U;
    if (!stop_ok)
    {
        enter_stopped(machine, PLAYBACK_STOP_REASON_AUDIO_FAULT, true);
    }
}

/**
 * @brief 归还手中与就绪队列队首连续的旧序号块（B1、B15）。
 * @param[in,out] machine 状态机。
 * @note  最多归还 PLAYBACK_MACHINE_STALE_BLOCK_LIMIT 块；当前序号的块留在队列里。
 */
static void discard_stale_blocks(Playback_MachineTypeDef *machine)
{
    Playback_MachineBlockTypeDef block;

    if (machine->Holding && ((machine->StreamSeq == 0U) || (machine->Held.StreamSeq != machine->StreamSeq)))
    {
        return_held_block(machine);
    }

    for (uint32_t i = 0U; i < PLAYBACK_MACHINE_STALE_BLOCK_LIMIT; i++)
    {
        if (!machine->Ops->Storage.PeekReadyBlock(&block))
        {
            break;
        }
        if ((machine->StreamSeq != 0U) && (block.StreamSeq == machine->StreamSeq))
        {
            break;
        }
        if (!machine->Ops->Storage.TakeReadyBlock(&block))
        {
            break;
        }
        machine->Ops->Storage.ReturnBlock(&block);
    }
}

/**
 * @brief 进入停止态：关流、归还手中块、记录原因并让停止次数加一。
 * @param[in,out] machine 状态机。
 * @param[in] reason 停止原因。
 * @param[in] keep_cursor true 保留游标，false 重置为无当前曲。
 * @note  B6 的每一个停止出口都经这里：先写“无流”，再 Stop，再记录原因（B7、B10）。Stop 失败时原因改为
 *        AUDIO_FAULT 并锁存（游标按原来的取舍）。进入停止态清零连跳计数与两个计时（B5）。
 */
static void enter_stopped(Playback_MachineTypeDef *machine, Playback_StopReasonTypeDef reason, bool keep_cursor)
{
    bool had_player_stream = machine->StreamBegun;

    close_stream(machine);
    machine->OutputRunning = false;
    if (had_player_stream && !abandon_player_stream(machine))
    {
        reason = PLAYBACK_STOP_REASON_AUDIO_FAULT;
    }
    if (reason == PLAYBACK_STOP_REASON_AUDIO_FAULT)
    {
        machine->AudioFaultLatched = true;
    }
    machine->ConsecutiveSkips = 0U;
    return_held_block(machine);

    machine->Snapshot.State = PLAYBACK_STATE_STOPPED;
    machine->Snapshot.StopReason = reason;
    machine->Snapshot.PositionMs = 0U;
    machine->Snapshot.DurationMs = 0U;
    machine->Snapshot.StopCount++;
    if (machine->Ops->LogInfo != NULL)
    {
        char text[48];

        (void)snprintf(text, sizeof(text), "Stop reason=%s", stop_reason_name(reason));
        machine->Ops->LogInfo(text);
    }
    if (!keep_cursor)
    {
        machine->Snapshot.HasTrack = false;
        machine->Snapshot.Generation = 0U;
        machine->Snapshot.Index = 0U;
        machine->TrackCount = 0U;
    }
}

/**
 * @brief 游标环形步进一首；曲目数未知时不动。
 * @param[in,out] machine 状态机。
 * @param[in] forward true 下一首，false 上一首。
 * @return 游标是否移动（有当前曲且曲目数已知）。
 * @note  代次不变（游标与代次绑定）；末曲环回首曲，首曲上一首环到末曲。
 */
static bool step_cursor(Playback_MachineTypeDef *machine, bool forward)
{
    if (!machine->Snapshot.HasTrack || (machine->TrackCount == 0U))
    {
        return false;
    }

    if (forward)
    {
        machine->Snapshot.Index++;
        if (machine->Snapshot.Index >= machine->TrackCount)
        {
            machine->Snapshot.Index = 0U;
        }
    }
    else if (machine->Snapshot.Index == 0U)
    {
        machine->Snapshot.Index = (uint16_t)(machine->TrackCount - 1U);
    }
    else
    {
        machine->Snapshot.Index--;
    }
    return true;
}

/**
 * @brief 用户换曲命令：清零连跳计数并更新自动跳过的方向（B5）。
 * @param[in,out] machine 状态机。
 * @param[in] forward true 向后（NEXT、SELECT、停止态 TOGGLE），false 向前（PREV）。
 */
static void note_user_move(Playback_MachineTypeDef *machine, bool forward)
{
    machine->ConsecutiveSkips = 0U;
    machine->SkipForward = forward;
}

/**
 * @brief 从停止态开播：无游标时打开 {代次 0, 位置 0}，否则打开游标所指曲目。
 * @param[in,out] machine 状态机。
 */
static void start_from_stopped(Playback_MachineTypeDef *machine)
{
    if (!machine->Snapshot.HasTrack)
    {
        machine->Snapshot.HasTrack = true;
        machine->Snapshot.Generation = 0U;
        machine->Snapshot.Index = 0U;
        machine->TrackCount = 0U;
    }

    note_user_move(machine, true);
    machine->Snapshot.State = PLAYBACK_STATE_PLAYING;
    machine->Snapshot.StopReason = PLAYBACK_STOP_REASON_NONE;
    log_event(machine, "Start", true);
    switch_to_cursor_track(machine);
}

/**
 * @brief 处理 TOGGLE：方向由自身状态决定。
 * @param[in,out] machine 状态机。
 */
static void handle_toggle(Playback_MachineTypeDef *machine)
{
    switch (machine->Snapshot.State)
    {
    case PLAYBACK_STATE_PLAYING:
        if (machine->StreamBegun)
        {
            if (stop_failed(machine->Ops->Player.PauseOutput()))
            {
                enter_stopped(machine, PLAYBACK_STOP_REASON_AUDIO_FAULT, true);
                break;
            }
            machine->OutputRunning = false;
        }
        machine->Snapshot.State = PLAYBACK_STATE_PAUSED;
        log_event(machine, "Pause", false);
        break;

    case PLAYBACK_STATE_PAUSED:
        machine->Snapshot.State = PLAYBACK_STATE_PLAYING;
        log_event(machine, "Resume", false);
        if (machine->StreamBegun && (machine->Ops->Player.ResumeOutput() == SERVICE_OK))
        {
            /* 恢复即 Start（补满半区的罕见情形下由随后的 Pump 回报 Start）：T_out 从此刻起算。 */
            machine->OutputRunning = true;
            machine->OutputSinceMs = machine->NowMs;
        }
        break;

    case PLAYBACK_STATE_STOPPED:
    default:
        if (machine->AudioFaultLatched)
        {
            machine->Snapshot.StopCount++;   /* B10：锁存后不开流、不出声，只重新给出原因。 */
        }
        else
        {
            start_from_stopped(machine);
        }
        break;
    }
}

/**
 * @brief 处理 NEXT / PREV。
 * @param[in,out] machine 状态机。
 * @param[in] forward true 为 NEXT。
 * @note  播放：切歌并保持播放；暂停：切到新曲开头、保持暂停；停止：只移动游标、不出声；
 *        无游标（或曲目数未知）时忽略。
 */
static void handle_step(Playback_MachineTypeDef *machine, bool forward)
{
    if (!step_cursor(machine, forward))
    {
        return;
    }

    note_user_move(machine, forward);
    log_event(machine, forward ? "Next" : "Prev", true);
    if (machine->Snapshot.State != PLAYBACK_STATE_STOPPED)
    {
        switch_to_cursor_track(machine);
    }
}

/**
 * @brief 处理 SELECT：选中并播放，三态下都切到该曲开播；选中当前曲也从头重播（新流序号）。
 * @param[in,out] machine 状态机。
 * @param[in] command SELECT 命令，携带窗口代次与位置。
 * @note  代次未变时沿用已知曲目数，使紧随其后的 NEXT/PREV 仍可环形步进；代次变化则曲目数未知。
 */
static void handle_select(Playback_MachineTypeDef *machine, const Playback_CommandTypeDef *command)
{
    if (machine->AudioFaultLatched)
    {
        machine->Snapshot.StopCount++;   /* B10：锁存后不开流、游标不动，只重新给出原因。 */
        return;
    }

    if (!machine->Snapshot.HasTrack || (machine->Snapshot.Generation != command->Generation))
    {
        machine->TrackCount = 0U;
    }

    note_user_move(machine, true);
    machine->Snapshot.HasTrack = true;
    machine->Snapshot.Generation = command->Generation;
    machine->Snapshot.Index = command->Index;
    machine->Snapshot.State = PLAYBACK_STATE_PLAYING;
    machine->Snapshot.StopReason = PLAYBACK_STOP_REASON_NONE;
    log_event(machine, "Start", true);
    switch_to_cursor_track(machine);
}

/**
 * @brief 处理一条命令，并回显命令序号（含忽略的命令）。
 * @param[in,out] machine 状态机。
 * @param[in] command 命令。
 */
static void handle_command(Playback_MachineTypeDef *machine, const Playback_CommandTypeDef *command)
{
    switch (command->Kind)
    {
    case PLAYBACK_COMMAND_TOGGLE:
        handle_toggle(machine);
        break;

    case PLAYBACK_COMMAND_NEXT:
        handle_step(machine, true);
        break;

    case PLAYBACK_COMMAND_PREV:
        handle_step(machine, false);
        break;

    case PLAYBACK_COMMAND_SELECT:
        handle_select(machine, command);
        break;

    default:
        break;
    }

    machine->Snapshot.HandledCommandSeq = command->Seq;
    publish_if_changed(machine);
}

/**
 * @brief 读流记录信箱；只接受当前流序号的记录，旧序号的一律丢弃（B3）。
 * @param[in,out] machine 状态机。
 */
static void refresh_record(Playback_MachineTypeDef *machine)
{
    Playback_MachineRecordTypeDef record;

    machine->RecordValid = false;
    if ((machine->StreamSeq == 0U) || !machine->Ops->Storage.ReadRecord(&record) ||
        (record.StreamSeq != machine->StreamSeq))
    {
        return;
    }
    machine->Record = record;
    machine->RecordValid = true;
    if ((record.OpenResult != machine->SeenOpen) || (record.End != machine->SeenEnd))
    {
        machine->SeenOpen = record.OpenResult;
        machine->SeenEnd = record.End;
        machine->DataEvent = true;   /* 收到本流的应答：T_data 重新计时（B7）。 */
    }
}

/**
 * @brief 开启 OK：按应答重新初始化游标代次与曲目数 N，让播放模块开始新流（B4）。
 * @param[in,out] machine 状态机。
 * @note  暂停中换曲（或开启期间被暂停）时，BeginStream 之后立即暂停输出：推进只解首帧并预填，不出声（B12）。
 *        BeginStream 被拒（STOP_FAILED 已锁存，或上一条流 Stop 失败）或暂停时 Stop 失败，都按 AUDIO_FAULT 停止（B10）。
 */
static void begin_player_stream(Playback_MachineTypeDef *machine)
{
    machine->Snapshot.Generation = machine->Record.Generation;
    machine->TrackCount = machine->Record.TrackCount;
    if (machine->Ops->Player.BeginStream(machine->Record.AudioBytes) != SERVICE_OK)
    {
        enter_stopped(machine, PLAYBACK_STOP_REASON_AUDIO_FAULT, true);
        return;
    }
    machine->StreamBegun = true;
    if ((machine->Snapshot.State == PLAYBACK_STATE_PAUSED) && stop_failed(machine->Ops->Player.PauseOutput()))
    {
        enter_stopped(machine, PLAYBACK_STOP_REASON_AUDIO_FAULT, true);
    }
}

/**
 * @brief 跳过坏文件：SkippedBadCount 加一，沿跳过方向前进一首；整轮都坏则停止（B5、B6）。
 * @param[in,out] machine 状态机。
 * @note  曲目数 N 未知（最近没有开启 OK 带回过）时无法环形步进与计数，按 NO_TRACK 停止并重置游标（B5：N = 0 走 NO_TRACK）。
 *        连跳计数达到 N 即 ALL_BAD，保留游标；跳过后的新流请求走与切歌相同的路径，暂停态保持暂停（B12）。
 */
static void skip_bad_file(Playback_MachineTypeDef *machine)
{
    if (machine->TrackCount == 0U)
    {
        enter_stopped(machine, PLAYBACK_STOP_REASON_NO_TRACK, false);
        return;
    }

    machine->Snapshot.SkippedBadCount++;
    machine->ConsecutiveSkips++;
    log_event(machine, "Skip bad", true);
    if (machine->ConsecutiveSkips >= machine->TrackCount)
    {
        enter_stopped(machine, PLAYBACK_STOP_REASON_ALL_BAD, true);
        return;
    }

    (void)step_cursor(machine, machine->SkipForward);
    switch_to_cursor_track(machine);
}

/**
 * @brief 按当前流记录推进状态机：开启结果（B6 开启各行）与终止事实（B6 终止各行）。
 * @param[in,out] machine 状态机。
 * @note  BAD_FILE 的应答若带回代次与曲目数（Storage 已解析出曲目），采用它们；否则沿用最近一次开启 OK 的 N。
 *        每个停止出口之后 RecordValid 为假（流已关闭），后续检查随之跳过。
 */
static void handle_record(Playback_MachineTypeDef *machine)
{
    if (!machine->RecordValid)
    {
        return;
    }

    if (!machine->StreamBegun)
    {
        switch (machine->Record.OpenResult)
        {
        case PLAYBACK_MACHINE_OPEN_OK:
            begin_player_stream(machine);
            break;

        case PLAYBACK_MACHINE_OPEN_BAD_FILE:
            if (machine->Record.TrackCount != 0U)
            {
                machine->TrackCount = machine->Record.TrackCount;
                machine->Snapshot.Generation = machine->Record.Generation;
            }
            skip_bad_file(machine);
            break;

        case PLAYBACK_MACHINE_OPEN_IO_ERROR:
            enter_stopped(machine, PLAYBACK_STOP_REASON_READ_ERROR, true);
            break;

        case PLAYBACK_MACHINE_OPEN_NOT_READY:
            enter_stopped(machine, PLAYBACK_STOP_REASON_NOT_READY, false);
            break;

        case PLAYBACK_MACHINE_OPEN_STALE:
            enter_stopped(machine, PLAYBACK_STOP_REASON_CATALOG_CHANGED, false);
            break;

        case PLAYBACK_MACHINE_OPEN_NO_TRACK:
            enter_stopped(machine, PLAYBACK_STOP_REASON_NO_TRACK, false);
            break;

        case PLAYBACK_MACHINE_OPEN_PENDING:
        default:
            break;
        }
    }

    if (!machine->RecordValid)
    {
        return;
    }

    if (machine->Record.End == PLAYBACK_MACHINE_END_READ_ERROR)
    {
        enter_stopped(machine, PLAYBACK_STOP_REASON_READ_ERROR, true);
    }
    else if (machine->Record.End == PLAYBACK_MACHINE_END_CARD_REMOVED)
    {
        enter_stopped(machine, PLAYBACK_STOP_REASON_CARD_REMOVED, false);
    }
}

/**
 * @brief 就绪队列队首是否还有当前流的块。
 * @param[in] machine 状态机。
 * @return 有为 true。
 */
static bool ready_has_current_block(const Playback_MachineTypeDef *machine)
{
    Playback_MachineBlockTypeDef block;

    return machine->Ops->Storage.PeekReadyBlock(&block) && (block.StreamSeq == machine->StreamSeq);
}

/**
 * @brief 把手中块与队列里本流的块依次交给播放模块，直到窗口满或没有块（B1、B15）。
 * @param[in,out] machine 状态机。
 * @return 本次交入的字节数。
 * @note  同一时刻最多持一块；当前块有效字节全部拷进窗口后立即归还。
 */
static uint32_t feed_blocks(Playback_MachineTypeDef *machine)
{
    uint32_t total = 0U;

    for (uint32_t i = 0U; i < PLAYBACK_MACHINE_FEED_BLOCK_LIMIT; i++)
    {
        if (!machine->Holding)
        {
            if (!ready_has_current_block(machine) || !machine->Ops->Storage.TakeReadyBlock(&machine->Held))
            {
                break;
            }
            machine->Holding = true;
            machine->HeldOffset = 0U;
            machine->DataEvent = true;   /* 收到本流的块：T_data 重新计时（B7）。 */
        }

        uint32_t accepted = 0U;
        uint32_t remaining = machine->Held.ValidBytes - machine->HeldOffset;

        if (machine->Ops->Player.Feed(&machine->Held.Data[machine->HeldOffset], remaining, &accepted) != SERVICE_OK)
        {
            break;
        }
        total += accepted;
        machine->HeldOffset += accepted;
        if (machine->HeldOffset < machine->Held.ValidBytes)
        {
            break;
        }
        return_held_block(machine);
    }
    return total;
}

/**
 * @brief 输入读尽（流记录 ENDED 且队列中无本流的块、手中无块）时告知播放模块一次（B13 ①）。
 * @param[in,out] machine 状态机。
 */
static void finish_input_if_drained(Playback_MachineTypeDef *machine)
{
    if (machine->InputFinished || !machine->RecordValid || (machine->Record.End != PLAYBACK_MACHINE_END_ENDED))
    {
        return;
    }
    if (machine->Holding || ready_has_current_block(machine))
    {
        return;
    }

    (void)machine->Ops->Player.FinishInput();
    machine->InputFinished = true;
}

/**
 * @brief 播完自动前进：游标前进（末曲环回首曲），走与切歌相同的路径（B13 ⑤、B11）。
 * @param[in,out] machine 状态机。
 */
static void advance_after_stream_end(Playback_MachineTypeDef *machine)
{
    log_event(machine, "Finished", true);
    machine->SkipForward = true;
    (void)step_cursor(machine, true);
    log_event(machine, "Auto", true);
    switch_to_cursor_track(machine);
}

/**
 * @brief 把播放模块回报的事实并入快照与计时；故障事实按 B6 的表处理（B5–B8、B10）。
 * @param[in,out] machine 状态机。
 * @param[in] report 播放模块回报。
 * @note  优先级：STOP_FAILED（锁存）→ TRANSFER_ERROR（AUDIO_ERROR）→ Start 被拒、同步预算耗尽（跳过坏文件）→ 正常事实。
 *        连跳计数在输出真正 Start 时清零（OutputStarted），不在首帧清零：首帧后 Start 被拒的曲目，其拒绝可能出现在
 *        更晚的一次推进里，首帧清零会让整库不支持的采样率无界轮转而到不了 ALL_BAD（B5、B6）。
 *        欠载恢复时播放模块已 Stop，T_out 暂停到它回报 Start（B8：只在 Platform 运行时计时）。
 */
static void apply_pump_report(Playback_MachineTypeDef *machine, const Service_Playback_PumpReportTypeDef *report)
{
    machine->PumpNeedInput = report->NeedInput;

    if (report->StopFailed)
    {
        enter_stopped(machine, PLAYBACK_STOP_REASON_AUDIO_FAULT, true);
        return;
    }

    if (report->Underruns > 0U)
    {
        machine->OutputRunning = false;
    }
    if (report->OutputStarted)
    {
        machine->OutputRunning = true;
        machine->OutputSinceMs = machine->NowMs;
        machine->ConsecutiveSkips = 0U;
    }
    if (report->OutputProgress)
    {
        machine->OutputSinceMs = machine->NowMs;
    }

    if (report->TransferError)
    {
        enter_stopped(machine, PLAYBACK_STOP_REASON_AUDIO_ERROR, true);
        return;
    }
    if (report->StartRejected || report->SyncExhausted)
    {
        skip_bad_file(machine);
        return;
    }

    if (report->FirstFrame)
    {
        if (machine->Ops->LogInfo != NULL)
        {
            char text[48];

            (void)snprintf(text, sizeof(text), "Open idx=%u rate=%lu", (unsigned)machine->Snapshot.Index,
                           (unsigned long)report->StreamInfo.SampleRateHz);
            machine->Ops->LogInfo(text);
        }
        machine->Snapshot.DurationMs = report->StreamInfo.TotalDurationMs;
    }
    if (report->DecodedMs > machine->Snapshot.PositionMs)
    {
        machine->Snapshot.PositionMs = report->DecodedMs;
    }
    publish_if_changed(machine);

    if (report->StreamEnded)
    {
        machine->OutputRunning = false;
        advance_after_stream_end(machine);
    }
}

/**
 * @brief 交字节并推进输出管线，输入不够且仍有块可交时重复，直到没有进展（B1）。
 * @param[in,out] machine 状态机。
 */
static void feed_and_pump(Playback_MachineTypeDef *machine)
{
    for (uint32_t round = 0U; (round < PLAYBACK_MACHINE_FEED_ROUND_LIMIT) && machine->StreamBegun; round++)
    {
        Service_Playback_PumpReportTypeDef report;

        uint32_t fed = feed_blocks(machine);

        finish_input_if_drained(machine);
        memset(&report, 0, sizeof(report));

        Service_StatusTypeDef status = machine->Ops->Player.Pump(&report);

        if (status == SERVICE_ERROR)
        {
            /* 读状态或提交半区失败：输出不可信，按音频错误处理（Service_Playback_Pump 的约定）。 */
            enter_stopped(machine, PLAYBACK_STOP_REASON_AUDIO_ERROR, true);
            break;
        }
        if (status != SERVICE_OK)
        {
            break;
        }
        apply_pump_report(machine, &report);
        publish_if_changed(machine);
        if (!report.NeedInput || (fed == 0U))
        {
            break;
        }
    }
}

/**
 * @brief 经过时间是否已达 limit_ms（回绕安全）。
 * @param[in] now_ms 当前时刻。
 * @param[in] since_ms 起算时刻。
 * @param[in] limit_ms 期限。
 * @return 达到为 true。
 */
static bool elapsed_reached(uint32_t now_ms, uint32_t since_ms, uint32_t limit_ms)
{
    return (uint32_t)(now_ms - since_ms) >= limit_ms;
}

/**
 * @brief 当前是否处于“需要数据却没有”的阶段（B7 的计时条件）。
 * @param[in] machine 状态机。
 * @return 需要且没有为 true。
 * @note  等开启应答；或本流未终止、播放模块补半区时输入不够、手中无块且就绪队列没有本流的块（B16）。
 *        暂停且输出已满、排空（ENDED 之后）、停止时播放模块不会要输入，所以自然不计时。
 */
static bool waiting_for_data(const Playback_MachineTypeDef *machine)
{
    if (machine->StreamSeq == 0U)
    {
        return false;
    }
    if (!machine->StreamBegun)
    {
        return true;
    }
    if (machine->RecordValid && (machine->Record.End != PLAYBACK_MACHINE_END_NONE))
    {
        return false;
    }
    return machine->PumpNeedInput && !machine->Holding && !ready_has_current_block(machine);
}

/**
 * @brief T_out 与 T_data 的到期判定，在本次唤醒的推进之后做（B8、B7）。
 * @param[in,out] machine 状态机。
 * @note  T_out 到期与 TRANSFER_ERROR 同路：Stop → AUDIO_ERROR，保留游标。T_data 到期：写“无流” → Stop → NO_DATA，
 *        保留游标。起算时刻是进入等待的那次唤醒的时刻；收到本流的应答或块重新计时。
 */
static void check_timeouts(Playback_MachineTypeDef *machine)
{
    if (machine->StreamBegun && machine->OutputRunning &&
        elapsed_reached(machine->NowMs, machine->OutputSinceMs, PLAYBACK_MACHINE_OUTPUT_TIMEOUT_MS))
    {
        enter_stopped(machine, PLAYBACK_STOP_REASON_AUDIO_ERROR, true);
        return;
    }

    if (!waiting_for_data(machine))
    {
        machine->DataWaitActive = false;
        machine->DataEvent = false;
        return;
    }

    if (!machine->DataWaitActive || machine->DataEvent)
    {
        machine->DataWaitActive = true;
        machine->DataWaitSinceMs = machine->NowMs;
        machine->DataEvent = false;
        return;
    }

    if (elapsed_reached(machine->NowMs, machine->DataWaitSinceMs, PLAYBACK_MACHINE_DATA_TIMEOUT_MS))
    {
        enter_stopped(machine, PLAYBACK_STOP_REASON_NO_DATA, true);
    }
}

/**
 * @brief 初始化状态机：快照全零（停止、无当前曲），未向外发布任何内容。
 * @param[out] machine 状态机实例。
 * @param[in] ops 外部依赖；须在状态机整个生命周期内有效。
 */
void playback_machine_init(Playback_MachineTypeDef *machine, const Playback_MachineOpsTypeDef *ops)
{
    memset(machine, 0, sizeof(*machine));
    machine->Ops = ops;
    machine->SkipForward = true;
}

/**
 * @brief 一次唤醒的全部工作，顺序为 B1：流记录 → 旧块归还 → 命令 → 交字节 → 推进输出管线。
 * @param[in,out] machine 状态机。
 * @param[in] commands 本次唤醒从命令 FIFO 取空的命令（≤ 8 条，按入队顺序）；count 为 0 时可为 NULL。
 * @param[in] command_count 命令条数。
 * @param[in] now_ms 调用方传入的单调毫秒时刻（B9）；T_data、T_out 都按它判定。
 * @note  不阻塞、不碰 FatFs/LVGL、不同步等 Storage；播放模块的 Stop/Start 阻塞由其自身文档说明。
 */
void playback_machine_step(Playback_MachineTypeDef *machine,
                           const Playback_CommandTypeDef *commands,
                           uint32_t command_count,
                           uint32_t now_ms)
{
    machine->NowMs = now_ms;

    refresh_record(machine);
    handle_record(machine);
    publish_if_changed(machine);
    discard_stale_blocks(machine);

    for (uint32_t i = 0U; i < command_count; i++)
    {
        handle_command(machine, &commands[i]);
        refresh_record(machine);
        discard_stale_blocks(machine);
    }

    handle_record(machine);
    publish_if_changed(machine);
    feed_and_pump(machine);
    check_timeouts(machine);
    publish_if_changed(machine);
}
