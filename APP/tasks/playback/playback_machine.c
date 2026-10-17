/**
 * @file playback_machine.c
 * @brief 播放任务的纯 C 状态机：命令表、流序号、块交接、切歌清理顺序、暂停、播完自动前进与快照发布。
 * @note  一次唤醒的顺序按 #71 B1：流记录 → 归还旧序号块 → 命令 → 交字节 → 推进输出管线。
 *        坏文件跳过、T_data/T_out 超时、播放模块与 Platform 故障、AUDIO_FAULT 锁存和除“流中 READ_ERROR”之外的
 *        停止原因归下一票（#79）；本文件对这些事实不作反应。
 */

#include "APP/tasks/playback/playback_machine.h"

#include <stddef.h>
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
    (void)machine->Ops->Storage.WriteRequest(&request);
}

/**
 * @brief 切歌：按已更新的游标换到一条新流，顺序为 B11 ①–④。
 * @param[in,out] machine 状态机。
 * @note  ① 分配新序号并写请求信箱；② 放弃播放模块的流（Platform Stop，返回即交还半区）；
 *        ③ 归还手中旧块；④ 就绪队列中的旧序号块由 discard_stale_blocks() 取出即归还。
 *        位置回 0、总时长变为未知，在接受命令的同一刻生效（K4）。
 */
static void switch_to_cursor_track(Playback_MachineTypeDef *machine)
{
    bool had_player_stream = machine->StreamBegun;

    open_stream_for_cursor(machine);
    if (had_player_stream)
    {
        (void)machine->Ops->Player.AbandonStream();
    }
    return_held_block(machine);
    machine->Snapshot.PositionMs = 0U;
    machine->Snapshot.DurationMs = 0U;
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
 * @note  #78 只有流中 READ_ERROR 用到；其余原因的接入归 #79。
 */
static void enter_stopped(Playback_MachineTypeDef *machine, Playback_StopReasonTypeDef reason, bool keep_cursor)
{
    bool had_player_stream = machine->StreamBegun;

    close_stream(machine);
    if (had_player_stream)
    {
        (void)machine->Ops->Player.AbandonStream();
    }
    return_held_block(machine);

    machine->Snapshot.State = PLAYBACK_STATE_STOPPED;
    machine->Snapshot.StopReason = reason;
    machine->Snapshot.PositionMs = 0U;
    machine->Snapshot.DurationMs = 0U;
    machine->Snapshot.StopCount++;
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

    machine->Snapshot.State = PLAYBACK_STATE_PLAYING;
    machine->Snapshot.StopReason = PLAYBACK_STOP_REASON_NONE;
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
            (void)machine->Ops->Player.PauseOutput();
        }
        machine->Snapshot.State = PLAYBACK_STATE_PAUSED;
        break;

    case PLAYBACK_STATE_PAUSED:
        machine->Snapshot.State = PLAYBACK_STATE_PLAYING;
        if (machine->StreamBegun)
        {
            (void)machine->Ops->Player.ResumeOutput();
        }
        break;

    case PLAYBACK_STATE_STOPPED:
    default:
        start_from_stopped(machine);
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
    if (!machine->Snapshot.HasTrack || (machine->Snapshot.Generation != command->Generation))
    {
        machine->TrackCount = 0U;
    }

    machine->Snapshot.HasTrack = true;
    machine->Snapshot.Generation = command->Generation;
    machine->Snapshot.Index = command->Index;
    machine->Snapshot.State = PLAYBACK_STATE_PLAYING;
    machine->Snapshot.StopReason = PLAYBACK_STOP_REASON_NONE;
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
}

/**
 * @brief 开启 OK：按应答重新初始化游标代次与曲目数 N，让播放模块开始新流（B4）。
 * @param[in,out] machine 状态机。
 * @note  暂停中换曲（或开启期间被暂停）时，BeginStream 之后立即暂停输出：推进只解首帧并预填，不出声（B12）。
 */
static void begin_player_stream(Playback_MachineTypeDef *machine)
{
    machine->Snapshot.Generation = machine->Record.Generation;
    machine->TrackCount = machine->Record.TrackCount;
    (void)machine->Ops->Player.BeginStream(machine->Record.AudioBytes);
    machine->StreamBegun = true;
    if (machine->Snapshot.State == PLAYBACK_STATE_PAUSED)
    {
        (void)machine->Ops->Player.PauseOutput();
    }
}

/**
 * @brief 按当前流记录推进状态机：开启 OK 与流中读错误。
 * @param[in,out] machine 状态机。
 */
static void handle_record(Playback_MachineTypeDef *machine)
{
    if (!machine->RecordValid)
    {
        return;
    }

    if (!machine->StreamBegun && (machine->Record.OpenResult == PLAYBACK_MACHINE_OPEN_OK))
    {
        begin_player_stream(machine);
    }

    if (machine->StreamBegun && (machine->Record.End == PLAYBACK_MACHINE_END_READ_ERROR))
    {
        enter_stopped(machine, PLAYBACK_STOP_REASON_READ_ERROR, true);
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
    (void)step_cursor(machine, true);
    switch_to_cursor_track(machine);
}

/**
 * @brief 把播放模块回报的事实并入快照：首帧给出总时长，每次推进更新已解码时间；流尾放完则前进。
 * @param[in,out] machine 状态机。
 * @param[in] report 播放模块回报。
 */
static void apply_pump_report(Playback_MachineTypeDef *machine, const Service_Playback_PumpReportTypeDef *report)
{
    if (report->FirstFrame)
    {
        machine->Snapshot.DurationMs = report->StreamInfo.TotalDurationMs;
    }
    if (report->DecodedMs > machine->Snapshot.PositionMs)
    {
        machine->Snapshot.PositionMs = report->DecodedMs;
    }
    publish_if_changed(machine);

    if (report->StreamEnded)
    {
        advance_after_stream_end(machine);
        publish_if_changed(machine);
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
        if (machine->Ops->Player.Pump(&report) != SERVICE_OK)
        {
            break;
        }
        apply_pump_report(machine, &report);
        if (!report.NeedInput || (fed == 0U))
        {
            break;
        }
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
}

/**
 * @brief 一次唤醒的全部工作，顺序为 B1：流记录 → 旧块归还 → 命令 → 交字节 → 推进输出管线。
 * @param[in,out] machine 状态机。
 * @param[in] commands 本次唤醒从命令 FIFO 取空的命令（≤ 8 条，按入队顺序）；count 为 0 时可为 NULL。
 * @param[in] command_count 命令条数。
 * @param[in] now_ms 调用方传入的单调毫秒时刻（B9）；本票只记录，超时判定归 #79。
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
}
