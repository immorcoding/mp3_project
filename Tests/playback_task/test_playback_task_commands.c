/**
 ******************************************************************************
 * @file    test_playback_task_commands.c
 * @brief   playback_machine 命令表（K3）与快照（K4、K5）的主机行为测试：开播、暂停、切歌、选曲。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "playback_task_test_support.h"

#define TRACKS 5U
#define GENERATION 7U

/**
 * @brief 把状态机带到“停止态且保留游标”：播放中流记录报读错误，游标留在原处（K5 介质读错误）。
 * @param[in] generation 开启应答代次。
 * @param[in] tracks 开启应答曲目数。
 */
static void stop_with_cursor_kept(uint32_t generation, uint16_t tracks)
{
    world_start_playing(generation, tracks);
    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_READ_ERROR);
    world_wake();

    Playback_SnapshotTypeDef snapshot = world_snapshot();

    assert(snapshot.State == PLAYBACK_STATE_STOPPED);
    assert(snapshot.StopReason == PLAYBACK_STOP_REASON_READ_ERROR);
    assert(snapshot.HasTrack);
}

/* ---------------------------------------------------------------------------
 * 开机与停止态开播
 * ------------------------------------------------------------------------- */

/**
 * @brief 开机后没有发布过快照，GUI 读到全零（停止、无原因、无当前曲）。
 */
static void test_boot_snapshot_is_all_zero(void)
{
    world_reset();

    Playback_SnapshotTypeDef snapshot = world_snapshot();

    assert(snapshot.State == PLAYBACK_STATE_STOPPED);
    assert(snapshot.StopReason == PLAYBACK_STOP_REASON_NONE);
    assert(!snapshot.HasTrack);
    assert(snapshot.Generation == 0U);
    assert(snapshot.Index == 0U);
    assert(snapshot.PositionMs == 0U);
    assert(snapshot.DurationMs == 0U);
    assert(snapshot.SkippedBadCount == 0U);
    assert(snapshot.StopCount == 0U);
    assert(snapshot.HandledCommandSeq == 0U);
    assert(published_count() == 0U);
    assert(fake_storage_request_count() == 0U);
}

/**
 * @brief 停止态没有游标时按播放：打开 {代次 0, 位置 0}，三态立即报播放，曲目身份为尚未确认的 {0, 0}。
 */
static void test_toggle_when_stopped_without_cursor_opens_first_track(void)
{
    world_reset();

    world_send(PLAYBACK_COMMAND_TOGGLE);

    Playback_MachineRequestTypeDef request = fake_storage_last_request();
    Playback_SnapshotTypeDef snapshot = world_snapshot();

    assert(fake_storage_request_count() == 1U);
    assert(request.StreamSeq != 0U);
    assert(request.Generation == 0U);
    assert(request.Position == 0U);
    assert(snapshot.State == PLAYBACK_STATE_PLAYING);
    assert(snapshot.StopReason == PLAYBACK_STOP_REASON_NONE);
    assert(snapshot.HasTrack);
    assert(snapshot.Generation == 0U);
    assert(snapshot.Index == 0U);
    assert(snapshot.PositionMs == 0U);
    assert(snapshot.DurationMs == 0U);
    assert(snapshot.HandledCommandSeq == 1U);
}

/**
 * @brief 开启 OK 后播放模块用应答带回的音频字节数开新流，快照里的代次由“未确认”变为应答代次。
 */
static void test_open_ok_begins_player_stream_and_confirms_generation(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);

    world_open_ok(GENERATION, TRACKS);

    assert(fake_player_begin_count() == 1U);
    assert(fake_player_last_audio_bytes() == WORLD_AUDIO_BYTES);
    assert(world_snapshot().Generation == GENERATION);
    assert(world_snapshot().Index == 0U);
    assert(world_snapshot().DurationMs == 0U);
}

/**
 * @brief 首帧解出后才填总时长；其后每解出一帧位置前进并发布，位置不回退。
 */
static void test_first_frame_publishes_duration_then_position_advances_per_frame(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_open_ok(GENERATION, TRACKS);
    assert(world_snapshot().DurationMs == 0U);

    world_feed_block(30U);

    Playback_SnapshotTypeDef first = world_snapshot();

    assert(first.DurationMs == 180000U);
    assert(first.PositionMs == 52U);   /* 假播放模块每次推进 2 帧 × 26 ms。 */
    assert(first.State == PLAYBACK_STATE_PLAYING);

    uint32_t publishes = published_count();

    world_feed_block(20U);
    assert(world_snapshot().PositionMs > first.PositionMs);
    assert(published_count() > publishes);

    uint32_t last_position = world_snapshot().PositionMs;

    world_wake();
    assert(world_snapshot().PositionMs >= last_position);
}

/**
 * @brief 没有任何变化的唤醒不覆盖状态信箱。
 */
static void test_idle_wake_does_not_publish(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_wake();   /* 窗口里剩下的字节逐帧解完。 */
    world_wake();
    world_wake();

    uint32_t publishes = published_count();

    world_wake();
    assert(published_count() == publishes);
}

/* ---------------------------------------------------------------------------
 * K3：播放列
 * ------------------------------------------------------------------------- */

/**
 * @brief 播放中 TOGGLE：播放模块暂停输出，三态报暂停，位置冻结在已解码时间。
 */
static void test_toggle_while_playing_pauses_and_freezes_position(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);

    uint32_t position = world_snapshot().PositionMs;

    world_send(PLAYBACK_COMMAND_TOGGLE);

    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    assert(world_snapshot().PositionMs == position);
    assert(call_log_count_of(CALL_PAUSE) == 1U);
    assert(fake_player_is_paused());
    assert(world_snapshot().Index == 0U);
    assert(world_snapshot().DurationMs == 180000U);
}

/**
 * @brief 暂停中 TOGGLE：恢复输出，三态回到播放，不重开流。
 */
static void test_toggle_while_paused_resumes_without_reopening(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_TOGGLE);
    uint32_t requests = fake_storage_request_count();

    world_send(PLAYBACK_COMMAND_TOGGLE);

    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(call_log_count_of(CALL_RESUME) == 1U);
    assert(!fake_player_is_paused());
    assert(fake_storage_request_count() == requests);
    assert(fake_player_begin_count() == 1U);
}

/**
 * @brief 播放中 NEXT：游标前进，切歌后保持播放；位置回 0、总时长在接受命令的同一刻变为未知。
 */
static void test_next_while_playing_switches_track_and_keeps_playing(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    uint32_t old_seq = world_current_seq();

    world_send(PLAYBACK_COMMAND_NEXT);

    Playback_SnapshotTypeDef snapshot = world_snapshot();
    Playback_MachineRequestTypeDef request = fake_storage_last_request();

    assert(snapshot.State == PLAYBACK_STATE_PLAYING);
    assert(snapshot.HasTrack);
    assert(snapshot.Generation == GENERATION);
    assert(snapshot.Index == 1U);
    assert(snapshot.PositionMs == 0U);
    assert(snapshot.DurationMs == 0U);
    assert(request.StreamSeq > old_seq);
    assert(request.Generation == GENERATION);
    assert(request.Position == 1U);
}

/**
 * @brief 播放中 PREV：总是直接切到前一首，不做“超过 3 秒先回开头”。
 */
static void test_prev_while_playing_goes_to_previous_track_regardless_of_position(void)
{
    world_reset();
    world_send_select(GENERATION, 3U);
    world_open_ok(GENERATION, TRACKS);
    world_feed_block(30U);
    assert(world_snapshot().PositionMs > 0U);

    world_send(PLAYBACK_COMMAND_PREV);

    assert(world_snapshot().Index == 2U);
    assert(world_snapshot().PositionMs == 0U);
    assert(fake_storage_last_request().Position == 2U);
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
}

/**
 * @brief 末曲 NEXT 环回首曲，首曲 PREV 环到末曲。
 */
static void test_next_and_prev_step_around_the_ring(void)
{
    world_reset();
    world_send_select(GENERATION, TRACKS - 1U);
    world_open_ok(GENERATION, TRACKS);

    world_send(PLAYBACK_COMMAND_NEXT);
    assert(world_snapshot().Index == 0U);
    world_open_ok(GENERATION, TRACKS);

    world_send(PLAYBACK_COMMAND_PREV);
    assert(world_snapshot().Index == TRACKS - 1U);
    assert(fake_storage_last_request().Position == TRACKS - 1U);
}

/**
 * @brief 播放中 SELECT：切到该曲播放；选中当前曲也从头重播，并换新流序号。
 */
static void test_select_while_playing_switches_and_reselect_current_restarts(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    uint32_t seq_before = world_current_seq();

    world_send_select(GENERATION, 3U);
    assert(world_snapshot().Index == 3U);
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(fake_storage_last_request().Position == 3U);
    uint32_t seq_after_switch = world_current_seq();
    assert(seq_after_switch > seq_before);

    world_open_ok(GENERATION, TRACKS);
    world_feed_block(30U);
    assert(world_snapshot().PositionMs > 0U);

    world_send_select(GENERATION, 3U);

    assert(world_snapshot().Index == 3U);
    assert(world_snapshot().PositionMs == 0U);
    assert(world_snapshot().DurationMs == 0U);
    assert(world_current_seq() > seq_after_switch);
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
}

/* ---------------------------------------------------------------------------
 * K3：暂停列
 * ------------------------------------------------------------------------- */

/**
 * @brief 暂停中 NEXT：换到新曲开头，保持暂停；开启 OK 后在暂停中预填，得到总时长，不出声。
 */
static void test_next_while_paused_stays_paused_and_prefills_silently(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);

    world_send(PLAYBACK_COMMAND_NEXT);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    assert(world_snapshot().Index == 1U);
    assert(world_snapshot().PositionMs == 0U);
    assert(world_snapshot().DurationMs == 0U);

    world_open_ok(GENERATION, TRACKS);
    assert(fake_player_begin_count() == 2U);
    assert(fake_player_is_paused());
    assert(call_log_index_of(CALL_PAUSE, 1U) > call_log_index_of(CALL_BEGIN_STREAM, 1U));

    world_feed_block(30U);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    assert(world_snapshot().DurationMs == 180000U);
    assert(world_snapshot().Index == 1U);
    assert(call_log_count_of(CALL_RESUME) == 0U);
    assert(fake_player_is_paused());
}

/**
 * @brief 暂停中 PREV 与 NEXT 对称：保持暂停、游标反向环形步进。
 */
static void test_prev_while_paused_stays_paused(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_TOGGLE);

    world_send(PLAYBACK_COMMAND_PREV);

    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    assert(world_snapshot().Index == TRACKS - 1U);
    assert(fake_storage_last_request().Position == TRACKS - 1U);
}

/**
 * @brief 暂停中 SELECT：切到该曲并播放（SELECT 是“选中并播放”）。
 */
static void test_select_while_paused_switches_and_plays(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_TOGGLE);

    world_send_select(GENERATION, 2U);

    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().Index == 2U);
    assert(fake_storage_last_request().Position == 2U);

    world_open_ok(GENERATION, TRACKS);
    assert(!fake_player_is_paused());
}

/**
 * @brief 开启应答到达之前就按了暂停：开启 OK 后照样在暂停中预填，不出声。
 */
static void test_pause_before_open_ok_prefills_paused(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    assert(call_log_count_of(CALL_PAUSE) == 0U);

    world_open_ok(GENERATION, TRACKS);

    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    assert(fake_player_is_paused());
    assert(call_log_count_of(CALL_PAUSE) == 1U);
}

/* ---------------------------------------------------------------------------
 * K3：停止列
 * ------------------------------------------------------------------------- */

/**
 * @brief 流中读错误：停止、READ_ERROR、保留游标、停止次数加一、位置与总时长归零、请求信箱写“无流”。
 */
static void test_stream_read_error_stops_and_keeps_cursor(void)
{
    world_reset();
    world_send_select(GENERATION, 2U);
    world_open_ok(GENERATION, TRACKS);
    world_feed_block(30U);

    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_READ_ERROR);
    world_wake();

    Playback_SnapshotTypeDef snapshot = world_snapshot();

    assert(snapshot.State == PLAYBACK_STATE_STOPPED);
    assert(snapshot.StopReason == PLAYBACK_STOP_REASON_READ_ERROR);
    assert(snapshot.HasTrack);
    assert(snapshot.Generation == GENERATION);
    assert(snapshot.Index == 2U);
    assert(snapshot.PositionMs == 0U);
    assert(snapshot.DurationMs == 0U);
    assert(snapshot.StopCount == 1U);
    assert(fake_storage_last_request().StreamSeq == 0U);
    assert(!fake_player_stream_active());
    assert(fake_storage_outstanding() == 0U);
}

/**
 * @brief 停止态有游标时 TOGGLE：打开游标所指曲目开播，停止原因清空。
 */
static void test_toggle_when_stopped_with_cursor_opens_cursor_track(void)
{
    world_reset();
    world_send_select(GENERATION, 2U);
    world_open_ok(GENERATION, TRACKS);
    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_READ_ERROR);
    world_wake();
    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);

    world_send(PLAYBACK_COMMAND_TOGGLE);

    Playback_SnapshotTypeDef snapshot = world_snapshot();

    assert(snapshot.State == PLAYBACK_STATE_PLAYING);
    assert(snapshot.StopReason == PLAYBACK_STOP_REASON_NONE);
    assert(snapshot.Index == 2U);
    assert(snapshot.Generation == GENERATION);
    assert(snapshot.StopCount == 1U);
    assert(fake_storage_last_request().StreamSeq != 0U);
    assert(fake_storage_last_request().Generation == GENERATION);
    assert(fake_storage_last_request().Position == 2U);
}

/**
 * @brief 停止态 NEXT / PREV：只移动游标，不开流、不出声，停止次数与原因不变。
 */
static void test_next_and_prev_when_stopped_only_move_cursor(void)
{
    world_reset();
    stop_with_cursor_kept(GENERATION, TRACKS);
    uint32_t requests = fake_storage_request_count();
    uint32_t begins = fake_player_begin_count();

    world_send(PLAYBACK_COMMAND_NEXT);
    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().Index == 1U);

    world_send(PLAYBACK_COMMAND_PREV);
    world_send(PLAYBACK_COMMAND_PREV);
    assert(world_snapshot().Index == TRACKS - 1U);
    assert(world_snapshot().Generation == GENERATION);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_READ_ERROR);
    assert(world_snapshot().StopCount == 1U);
    assert(world_snapshot().PositionMs == 0U);
    assert(world_snapshot().DurationMs == 0U);
    assert(fake_storage_request_count() == requests);
    assert(fake_player_begin_count() == begins);
}

/**
 * @brief 停止态没有游标时 NEXT / PREV 被忽略（仍回显序号）。
 */
static void test_next_and_prev_when_stopped_without_cursor_are_ignored(void)
{
    world_reset();

    world_send(PLAYBACK_COMMAND_NEXT);
    world_send(PLAYBACK_COMMAND_PREV);

    Playback_SnapshotTypeDef snapshot = world_snapshot();

    assert(snapshot.State == PLAYBACK_STATE_STOPPED);
    assert(!snapshot.HasTrack);
    assert(snapshot.Generation == 0U);
    assert(snapshot.Index == 0U);
    assert(snapshot.StopCount == 0U);
    assert(snapshot.HandledCommandSeq == 2U);
    assert(fake_storage_request_count() == 0U);
}

/**
 * @brief 停止态 SELECT：打开该曲开播。
 */
static void test_select_when_stopped_opens_and_plays(void)
{
    world_reset();
    stop_with_cursor_kept(GENERATION, TRACKS);

    world_send_select(GENERATION, 4U);

    Playback_SnapshotTypeDef snapshot = world_snapshot();

    assert(snapshot.State == PLAYBACK_STATE_PLAYING);
    assert(snapshot.StopReason == PLAYBACK_STOP_REASON_NONE);
    assert(snapshot.Index == 4U);
    assert(fake_storage_last_request().Position == 4U);
    assert(fake_storage_last_request().StreamSeq != 0U);
}

/* ---------------------------------------------------------------------------
 * 命令序号回显与同一次唤醒的多条命令
 * ------------------------------------------------------------------------- */

/**
 * @brief 同一次唤醒里的命令按顺序处理，含被忽略的；命令序号回显最后一条。
 */
static void test_handled_command_seq_echoes_last_command_including_ignored(void)
{
    world_reset();
    Playback_CommandTypeDef commands[3] = {
        { PLAYBACK_COMMAND_NEXT, 5U, 0U, 0U },     /* 无游标，忽略。 */
        { PLAYBACK_COMMAND_PREV, 6U, 0U, 0U },     /* 无游标，忽略。 */
        { PLAYBACK_COMMAND_TOGGLE, 7U, 0U, 0U }
    };

    world_step_commands(commands, 3U);

    assert(world_snapshot().HandledCommandSeq == 7U);
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(fake_storage_request_count() == 1U);
}

/**
 * @brief 连按 N 次 NEXT 就是 N 条命令、游标走 N 步；每条都写一次请求，最后一次覆盖前面的。
 */
static void test_burst_of_next_steps_cursor_once_per_command(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    uint32_t requests = fake_storage_request_count();
    Playback_CommandTypeDef commands[3] = {
        { PLAYBACK_COMMAND_NEXT, 10U, 0U, 0U },
        { PLAYBACK_COMMAND_NEXT, 11U, 0U, 0U },
        { PLAYBACK_COMMAND_NEXT, 12U, 0U, 0U }
    };

    world_step_commands(commands, 3U);

    assert(world_snapshot().Index == 3U);
    assert(world_snapshot().HandledCommandSeq == 12U);
    assert(fake_storage_request_count() == requests + 3U);
    assert(fake_storage_last_request().Position == 3U);
}

/**
 * @brief 运行全部命令表测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_boot_snapshot_is_all_zero();
    test_toggle_when_stopped_without_cursor_opens_first_track();
    test_open_ok_begins_player_stream_and_confirms_generation();
    test_first_frame_publishes_duration_then_position_advances_per_frame();
    test_idle_wake_does_not_publish();
    test_toggle_while_playing_pauses_and_freezes_position();
    test_toggle_while_paused_resumes_without_reopening();
    test_next_while_playing_switches_track_and_keeps_playing();
    test_prev_while_playing_goes_to_previous_track_regardless_of_position();
    test_next_and_prev_step_around_the_ring();
    test_select_while_playing_switches_and_reselect_current_restarts();
    test_next_while_paused_stays_paused_and_prefills_silently();
    test_prev_while_paused_stays_paused();
    test_select_while_paused_switches_and_plays();
    test_pause_before_open_ok_prefills_paused();
    test_stream_read_error_stops_and_keeps_cursor();
    test_toggle_when_stopped_with_cursor_opens_cursor_track();
    test_next_and_prev_when_stopped_only_move_cursor();
    test_next_and_prev_when_stopped_without_cursor_are_ignored();
    test_select_when_stopped_opens_and_plays();
    test_handled_command_seq_echoes_last_command_including_ignored();
    test_burst_of_next_steps_cursor_once_per_command();

    puts("playback_task_commands: all tests passed");
    return 0;
}
