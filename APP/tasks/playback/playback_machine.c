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
           (a->HandledCommandSeq == b->HandledCommandSeq) &&
           (a->TrackStartCount == b->TrackStartCount);
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
 * @brief 记一条 INFO 日志：事件加曲库序号（可选）与一个可选数值，格式 "<事件> [idx=<序号>] [<键>=<值>]"。
 * @param[in] machine 状态机。
 * @param[in] event 事件名，静态字符串。
 * @param[in] with_index 是否附当前游标的曲库序号。
 * @param[in] key 数值的键名；NULL 表示不附数值。
 * @param[in] value 数值（key 非 NULL 时有效）。
 * @note  只在命令与曲目边界调用；不记文件名（曲名属于曲名数据路径）。所有 INFO 事件共用本格式化路径；
 *        缓冲放不下时末字符改为 '~'，截断可见而不静默。
 */
static void log_event_value(const Playback_MachineTypeDef *machine, const char *event, bool with_index,
                            const char *key, unsigned long value)
{
    char text[PLAYBACK_MACHINE_LOG_TEXT_SIZE];
    size_t used;
    int written;

    if (machine->Ops->LogInfo == NULL)
    {
        return;
    }

    written = snprintf(text, sizeof(text), "%s", event);
    if (with_index && (written >= 0) && ((size_t)written < sizeof(text)))
    {
        used = (size_t)written;
        written += snprintf(&text[used], sizeof(text) - used, " idx=%u", (unsigned)machine->Snapshot.Index);
    }
    if ((key != NULL) && (written >= 0) && ((size_t)written < sizeof(text)))
    {
        used = (size_t)written;
        written += snprintf(&text[used], sizeof(text) - used, " %s=%lu", key, value);
    }
    if ((written < 0) || ((size_t)written >= sizeof(text)))
    {
        text[sizeof(text) - 2U] = '~';
        text[sizeof(text) - 1U] = '\0';
    }
    machine->Ops->LogInfo(text);
}

/**
 * @brief 记一条不带数值的 INFO 日志。
 * @param[in] machine 状态机。
 * @param[in] event 事件名，静态字符串。
 * @param[in] with_index 是否附当前游标的曲库序号。
 */
static void log_event(const Playback_MachineTypeDef *machine, const char *event, bool with_index)
{
    log_event_value(machine, event, with_index, NULL, 0UL);
}

/**
 * @brief 记一条 DEBUG 日志（被静默忽略的命令）。
 * @param[in] machine 状态机。
 * @param[in] text 日志文本，静态字符串。
 */
static void log_debug(const Playback_MachineTypeDef *machine, const char *text)
{
    if (machine->Ops->LogDebug != NULL)
    {
        machine->Ops->LogDebug(text);
    }
}

/**
 * @brief 作废尚未完成的跳转：不再重开、不再记 Seek INFO，位置不再拒收（切歌与停止都走这里）。
 * @param[in,out] machine 状态机。
 */
static void cancel_seek(Playback_MachineTypeDef *machine)
{
    machine->SeekActive = false;
    machine->SeekAwaitReopen = false;
    machine->SeekHoldEnd = false;
}

/**
 * @brief 记 Seek INFO：执行成功后的结果，格式见 playback README；lat 为处理 SEEK 命令到现在的周期差，经 CyclesToUs 换算。
 * @param[in] machine 状态机。
 * @note  只含曲库序号与数值，不含文件名。
 */
static void log_seek_done(const Playback_MachineTypeDef *machine)
{
    char text[PLAYBACK_MACHINE_LOG_TEXT_SIZE];
    unsigned long latency_us = 0UL;

    if (machine->Ops->LogInfo == NULL)
    {
        return;
    }
    if ((machine->Ops->ReadCycles != NULL) && (machine->Ops->CyclesToUs != NULL))
    {
        latency_us = (unsigned long)machine->Ops->CyclesToUs(machine->Ops->ReadCycles() - machine->SeekStartCycles);
    }

    (void)snprintf(text, sizeof(text), "Seek idx=%u tgt=%lu land=%lu off=%lu map=%s%s lat=%lu",
                   (unsigned)machine->Snapshot.Index,
                   (unsigned long)machine->SeekTargetMs,
                   (unsigned long)machine->SeekPoint.LandingMs,
                   (unsigned long)machine->SeekPoint.AudioOffset,
                   (machine->SeekPoint.Map == SERVICE_PLAYBACK_SEEK_MAP_TOC) ? "toc" : "lin",
                   machine->SeekPoint.Clamped ? " clamp" : "",
                   latency_us);
    machine->Ops->LogInfo(text);
}

/**
 * @brief 记 Switch INFO：换曲（用户命令或自动前进）被决定到输出重新起播的时延（DIAG-6），每次换曲一行，自动前进带 auto 后缀。
 * @param[in,out] machine 状态机。
 * @note  时延 = 命令被本状态机处理时的周期计数器读数到现在的差，经 CyclesToUs 换算为微秒；Ops 缺 ReadCycles
 *        或 CyclesToUs 时记 0。只在换曲边界调用，不进每帧路径；文本只含曲库序号与数值。
 */
static void log_switch_done(Playback_MachineTypeDef *machine)
{
    char text[PLAYBACK_MACHINE_LOG_TEXT_SIZE];
    unsigned long latency_us = 0UL;

    if (!machine->SwitchActive)
    {
        return;
    }

    machine->SwitchActive = false;
    if (machine->Ops->LogInfo == NULL)
    {
        return;
    }
    if ((machine->Ops->ReadCycles != NULL) && (machine->Ops->CyclesToUs != NULL))
    {
        latency_us = (unsigned long)machine->Ops->CyclesToUs(machine->Ops->ReadCycles() - machine->SwitchStartCycles);
    }

    (void)snprintf(text, sizeof(text), "Switch idx=%u lat=%lu%s",
                   (unsigned)machine->Snapshot.Index,
                   latency_us,
                   machine->SwitchAuto ? " auto" : "");
    machine->Ops->LogInfo(text);
}

/**
 * @brief 播放中自动换到下一首（播完前进、跳过坏文件续上）时开始计 Switch 的时延；已有等待起播的换曲则保持原起点。
 * @param[in,out] machine 状态机。
 * @param[in] start_cycles 决定前进那一刻的周期计数器读数。
 * @note  用户换曲优先：已在等待的用户换曲不被改成 auto；跳过坏文件接在用户换曲或自动前进之后时沿用原起点与原种类。
 *        只在 PLAYING 下计（暂停中前进不出声）；Ops 缺 ReadCycles 时起点为 0，lat 记 0。
 */
static void begin_auto_switch(Playback_MachineTypeDef *machine, uint32_t start_cycles)
{
    if (machine->SwitchActive || (machine->Snapshot.State != PLAYBACK_STATE_PLAYING))
    {
        return;
    }

    machine->SwitchActive = true;
    machine->SwitchAuto = true;
    machine->SwitchStartCycles = start_cycles;
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
 * @param[in] audio_offset 相对音频区间起点的字节数：切歌与普通开流为 0，跳转为映射给出的偏移。
 * @note  不碰播放模块；旧流的清理由调用者在写请求之后做（B11）。
 */
static void open_stream_for_cursor(Playback_MachineTypeDef *machine, uint32_t audio_offset)
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
    request.AudioOffset = audio_offset;
    (void)machine->Ops->Storage.WriteRequest(&request);
}

/**
 * @brief 写“无流”请求，让音频读取任务关闭文件并忽略此前的请求。
 * @param[in,out] machine 状态机。
 */
static void close_stream(Playback_MachineTypeDef *machine)
{
    Playback_MachineRequestTypeDef request;

    request.StreamSeq = 0U;
    request.Generation = 0U;
    request.Position = 0U;
    request.AudioOffset = 0U;
    cancel_seek(machine);
    machine->StreamSeq = 0U;
    machine->StreamBegun = false;
    machine->InputFinished = false;
    machine->RecordValid = false;
    reset_stream_tracking(machine);
    (void)machine->Ops->Storage.WriteRequest(&request);
}

static void enter_stopped(Playback_MachineTypeDef *machine, Playback_StopReasonTypeDef reason, bool keep_cursor);

/**
 * @brief 换到游标曲目的新流（从 audio_offset 起），并清掉旧流：B11 ①–③ 的共用序列，切歌与跳转都走它。
 * @param[in,out] machine 状态机。
 * @param[in] audio_offset 相对音频区间的起始偏移；切歌为 0，跳转为映射出的偏移。
 * @return 放弃旧流时 Stop 成功（或没有旧流）为 true；Stop 失败（STOP_FAILED 锁存）为 false，调用者按 AUDIO_FAULT 停止（B10）。
 * @note  ① 分配新序号并写请求信箱；② 放弃播放模块的流（Platform Stop，返回即交还半区）；③ 归还手中旧块。
 *        Stop 失败时新流请求仍已写出，由调用者随后的 enter_stopped() 以“无流”覆盖。
 */
static bool restart_stream_at(Playback_MachineTypeDef *machine, uint32_t audio_offset)
{
    const bool had_player_stream = machine->StreamBegun;
    bool stop_ok = true;

    open_stream_for_cursor(machine, audio_offset);
    if (had_player_stream)
    {
        stop_ok = abandon_player_stream(machine);
    }
    return_held_block(machine);
    return stop_ok;
}

/**
 * @brief 切歌：按已更新的游标换到一条新流，顺序为 B11 ①–④。
 * @param[in,out] machine 状态机。
 * @note  ① 分配新序号并写请求信箱；② 放弃播放模块的流（Platform Stop，返回即交还半区）；
 *        ③ 归还手中旧块；④ 就绪队列中的旧序号块由 discard_stale_blocks() 取出即归还。
 *        位置回 0、总时长变为未知，在接受命令的同一刻生效（K4）；每次切歌使 TrackStartCount 加 1（跳转不走这里）。
 *        ② 里 Stop 失败时，已写的新流请求被“无流”覆盖，停止为 AUDIO_FAULT（B10）。
 */
static void switch_to_cursor_track(Playback_MachineTypeDef *machine)
{
    bool stop_ok;

    cancel_seek(machine);
    machine->Snapshot.TrackStartCount++;
    stop_ok = restart_stream_at(machine, 0U);
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
    machine->SwitchActive = false;
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
        if (!machine->StreamBegun)
        {
            log_event(machine, "Resume", false);
        }
        else if (machine->Ops->Player.ResumeOutput() == SERVICE_OK)
        {
            /* 恢复即 Start（补满半区的罕见情形下由随后的 Pump 回报 Start）：T_out 从此刻起算。 */
            machine->OutputRunning = true;
            machine->OutputSinceMs = machine->NowMs;
            log_event(machine, "Resume", false);
        }
        /* ResumeOutput 失败不记 Resume：输出未恢复，随后由 T_data/T_out 超时走停止日志。 */
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
 * @brief 处理 SEEK：核对曲目身份与总时长，映射出落点，走切歌的清理路径带偏移重开流（#166）。
 * @param[in,out] machine 状态机。
 * @param[in] command SEEK 命令：Generation/Index 是 GUI 按下时显示的曲目，TargetMs 是目标毫秒。
 * @note  三种静默忽略（无当前曲或已停止、曲目不符、总时长 0）与映射失败只记 DEBUG，不改三态、不计停止。
 *        执行时：新流序号与偏移写进请求信箱，放弃播放模块的旧流（Stop 失败按 AUDIO_FAULT 停止，与切歌一致），
 *        归还手中旧块，并在同一步把 PositionMs 设为落点（允许变小）；回显序号由调用者随后推进，所以 GUI 看到回显时
 *        快照已是落点。不改三态、不清 DurationMs、不加 TrackStartCount。已有未完成的跳转被这条覆盖（旧流序号作废）。
 *        新流开启 OK 前 SeekAwaitReopen 为真，拒收一切 DecodedMs；Seek INFO 等输出重新起播或预填完成。
 */
static void handle_seek(Playback_MachineTypeDef *machine, const Playback_CommandTypeDef *command)
{
    Service_Playback_SeekPointTypeDef point;
    bool stop_ok;

    if (!machine->Snapshot.HasTrack || (machine->Snapshot.State == PLAYBACK_STATE_STOPPED))
    {
        log_debug(machine, "Seek ignored: no current track");
        return;
    }
    if ((machine->Snapshot.Generation != command->Generation) || (machine->Snapshot.Index != command->Index))
    {
        log_debug(machine, "Seek ignored: track mismatch");
        return;
    }
    if (machine->Snapshot.DurationMs == 0U)
    {
        log_debug(machine, "Seek ignored: duration unknown");
        return;
    }
    if (machine->Ops->Player.MapSeek(command->TargetMs, &point) != SERVICE_OK)
    {
        log_debug(machine, "Seek ignored: not mappable");
        return;
    }

    machine->SeekStartCycles = (machine->Ops->ReadCycles != NULL) ? machine->Ops->ReadCycles() : 0U;
    stop_ok = restart_stream_at(machine, point.AudioOffset);

    machine->SeekActive = true;
    machine->SeekAwaitReopen = true;
    machine->SeekPoint = point;
    machine->SeekTargetMs = command->TargetMs;
    machine->Snapshot.PositionMs = point.LandingMs;
    if (!stop_ok)
    {
        enter_stopped(machine, PLAYBACK_STOP_REASON_AUDIO_FAULT, true);
    }
}

/**
 * @brief 处理一条命令，并回显命令序号（含忽略的命令）。
 * @param[in,out] machine 状态机。
 * @param[in] command 命令。
 */
static void handle_command(Playback_MachineTypeDef *machine, const Playback_CommandTypeDef *command)
{
    const bool is_switch_kind = (command->Kind == PLAYBACK_COMMAND_NEXT) ||
                                (command->Kind == PLAYBACK_COMMAND_PREV) ||
                                (command->Kind == PLAYBACK_COMMAND_SELECT);
    const uint32_t switch_start_cycles = (is_switch_kind && (machine->Ops->ReadCycles != NULL)) ?
                                             machine->Ops->ReadCycles() : 0U;
    const uint32_t starts_before = machine->Snapshot.TrackStartCount;

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

    case PLAYBACK_COMMAND_SEEK:
        handle_seek(machine, command);
        break;

    default:
        break;
    }

    if (is_switch_kind && (machine->Snapshot.TrackStartCount != starts_before) &&
        (machine->Snapshot.State == PLAYBACK_STATE_PLAYING))
    {
        /* 真正换了一首且在放：等输出重新起播再记；最新一次换曲覆盖未完成的上一次。 */
        machine->SwitchActive = true;
        machine->SwitchAuto = false;
        machine->SwitchStartCycles = switch_start_cycles;
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
 * @note  跳转的新流用 ReopenStream（落点来自映射）代替 BeginStream，其余相同；
 *        暂停中换曲或跳转（或开启期间被暂停）时，开流之后立即暂停输出：推进只解首帧并预填，不出声（B12）。
 *        BeginStream 被拒（STOP_FAILED 已锁存，或上一条流 Stop 失败）或暂停时 Stop 失败，都按 AUDIO_FAULT 停止（B10）。
 */
static void begin_player_stream(Playback_MachineTypeDef *machine)
{
    machine->Snapshot.Generation = machine->Record.Generation;
    machine->TrackCount = machine->Record.TrackCount;
    if (machine->SeekAwaitReopen)
    {
        /* 跳转的新流：从落点重开，保留流级事实（总时长、采样率、TOC），不重新探测流头。 */
        if (machine->Ops->Player.ReopenStream(machine->SeekPoint.LandingMs) != SERVICE_OK)
        {
            enter_stopped(machine, PLAYBACK_STOP_REASON_AUDIO_FAULT, true);
            return;
        }
        machine->SeekAwaitReopen = false;
        machine->SeekHoldEnd = true;
    }
    else if (machine->Ops->Player.BeginStream(machine->Record.AudioBytes) != SERVICE_OK)
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
    const uint32_t start_cycles = (machine->Ops->ReadCycles != NULL) ? machine->Ops->ReadCycles() : 0U;

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
    begin_auto_switch(machine, start_cycles);
}

/**
 * @brief 按当前流记录推进状态机：开启结果（B6 开启各行）与终止事实（B6 终止各行）。
 * @param[in,out] machine 状态机。
 * @note  BAD_FILE 的应答若带回代次与曲目数（音频读取任务已解析出曲目），采用它们；否则沿用最近一次开启 OK 的 N。
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
    const uint32_t start_cycles = (machine->Ops->ReadCycles != NULL) ? machine->Ops->ReadCycles() : 0U;
    bool moved;

    log_event(machine, "Finished", true);
    machine->SkipForward = true;
    moved = step_cursor(machine, true);
    switch_to_cursor_track(machine);
    if (moved && (machine->Snapshot.State != PLAYBACK_STATE_STOPPED))
    {
        log_event(machine, "Auto", true);
        /* 播完前进是曲目边界：新曲起播时记 Switch ... auto（长跑里没有用户切歌，这一行是唯一的边界时延）。 */
        begin_auto_switch(machine, start_cycles);
    }
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
        machine->UnderrunTotal += report->Underruns;
    }
    if (report->OutputStarted)
    {
        machine->SeekHoldEnd = false;
        machine->OutputRunning = true;
        machine->OutputSinceMs = machine->NowMs;
        machine->ConsecutiveSkips = 0U;
        log_switch_done(machine);
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
        log_event_value(machine, "Open", true, "rate", (unsigned long)report->StreamInfo.SampleRateHz);
        machine->Snapshot.DurationMs = report->StreamInfo.TotalDurationMs;
    }
    if (machine->SeekActive && !machine->SeekAwaitReopen &&
        (report->OutputStarted ||
         ((machine->Snapshot.State == PLAYBACK_STATE_PAUSED) && !report->NeedInput &&
          (!report->StreamEnded || machine->SeekHoldEnd))))
    {
        /* PLAYING：输出重新起播；PAUSED：预填完成（补半区不再要输入，且没有读尽到无音频可放）。 */
        log_seek_done(machine);
        machine->SeekActive = false;
    }
    if (!machine->SeekAwaitReopen && (report->DecodedMs > machine->Snapshot.PositionMs))
    {
        machine->Snapshot.PositionMs = report->DecodedMs;
    }
    publish_if_changed(machine);

    if (report->StreamEnded && (machine->Snapshot.State == PLAYBACK_STATE_PAUSED) && machine->SeekHoldEnd)
    {
        /* 暂停中跳转到末尾：钳位后只剩不足一帧，播放模块没有可放的音频就报流尾放完（状态类事实，每次重复）。
         * 停在末尾保持 PAUSED、位置为落点；恢复后同一事实再次到来，按播完前进。 */
        machine->OutputRunning = false;
    }
    else if (report->StreamEnded)
    {
        machine->OutputRunning = false;
        if (machine->SeekActive && !machine->SeekAwaitReopen)
        {
            /* PLAYING 下越尾且钳位后零帧：没有起播也没有预填可等，前进会作废跳转，所以前进之前补记这一行。 */
            log_seek_done(machine);
            machine->SeekActive = false;
        }
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
 * @brief 读开机以来播放模块回报的欠载次数累计（DIAG-6）。
 * @param[in] machine 状态机实例。
 * @return 累计次数；Platform 的计数每次 Start 清零，这里在状态机里累加。
 * @note  写者只有播放任务；32 位对齐读取在 Cortex-M7 上不撕裂，其他任务（Monitor）可只读。
 */
uint32_t playback_machine_underrun_total(const Playback_MachineTypeDef *machine)
{
    return machine->UnderrunTotal;
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
 * @note  不阻塞、不碰 FatFs/LVGL、不同步等音频读取任务；播放模块的 Stop/Start 阻塞由其自身文档说明。
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
        if ((commands[i].Kind == PLAYBACK_COMMAND_SEEK) && ((i + 1U) < command_count) &&
            (commands[i + 1U].Kind == PLAYBACK_COMMAND_SEEK))
        {
            /* 最新者胜：同批连续的 SEEK 只执行最后一条；被合并的不执行、不记日志，但回显序号照常推进。 */
            machine->Snapshot.HandledCommandSeq = commands[i].Seq;
            publish_if_changed(machine);
            continue;
        }
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
