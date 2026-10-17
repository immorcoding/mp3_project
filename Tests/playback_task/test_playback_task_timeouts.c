/**
 ******************************************************************************
 * @file    test_playback_task_timeouts.c
 * @brief   playback_machine 的 T_data 数据等待超时（#71 B7）与 T_out 输出无进展看门狗（B8）的主机行为测试：
 *          起算、重新计时、不计时的阶段，以及到期后“写无流 → Stop → 停止”。时刻全部由测试传入（B9）。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "APP/tasks/playback/playback_task_config.h"
#include "playback_task_test_support.h"

#define T_DATA PLAYBACK_MACHINE_DATA_TIMEOUT_MS
#define T_OUT PLAYBACK_MACHINE_OUTPUT_TIMEOUT_MS
#define TRACKS 4U
#define GENERATION 7U

/**
 * @brief 断言仍在播放（没有停止，停止次数为 0）。
 */
static void expect_still_playing(void)
{
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().StopCount == 0U);
}

/**
 * @brief 断言已因 reason 停止：停止次数 1，游标保留，请求信箱写成“无流”，播放模块已放弃流。
 * @param[in] reason 期望的停止原因。
 * @param[in] index 期望保留的游标位置。
 */
static void expect_timed_out_with(Playback_StopReasonTypeDef reason, uint16_t index)
{
    Playback_SnapshotTypeDef snapshot = world_snapshot();

    assert(snapshot.State == PLAYBACK_STATE_STOPPED);
    assert(snapshot.StopReason == reason);
    assert(snapshot.StopCount == 1U);
    assert(snapshot.HasTrack);
    assert(snapshot.Generation == GENERATION);
    assert(snapshot.Index == index);
    assert(snapshot.PositionMs == 0U);
    assert(snapshot.DurationMs == 0U);
    assert(fake_storage_last_request().StreamSeq == 0U);
    assert(!fake_player_stream_active());
}

/**
 * @brief 把状态机带到“正在播放且输入已用尽”：首帧已出，播放模块在补半区时报输入不够，T_data 由此刻起算。
 * @return 起算时刻。
 */
static uint32_t start_starved_playing(void)
{
    world_start_playing(GENERATION, TRACKS);
    world_wake();   /* 吃完窗口里剩下的一帧，随后回报输入不够。 */
    expect_still_playing();
    return world_now();
}

/* ---------------------------------------------------------------------------
 * T_data
 * ------------------------------------------------------------------------- */

/**
 * @brief 等开启应答从写请求信箱起算：T_data 差 1 ms 时仍在等，到期则写“无流”并停止 NO_DATA，游标保留。
 */
static void test_open_reply_wait_expires_after_t_data(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    uint32_t t0 = world_now();

    world_wake_at(t0 + T_DATA - 1U);
    expect_still_playing();
    world_wake_at(t0 + T_DATA);

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_NO_DATA);
    assert(world_snapshot().StopCount == 1U);
    assert(world_snapshot().HasTrack);   /* 游标保留（代次 0 尚未确认） */
    assert(fake_storage_last_request().StreamSeq == 0U);
}

/**
 * @brief 收到本流的开启应答重新计时：应答之后再等满 T_data 才到期，而不是从最初的请求起算。
 */
static void test_open_reply_restarts_the_clock(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    uint32_t t0 = world_now();

    world_wake_at(t0 + (T_DATA / 2U));
    world_open_ok(GENERATION, TRACKS);   /* 应答到达，但没有数据 */
    uint32_t t_ok = world_now();

    world_wake_at(t_ok + T_DATA - 1U);
    expect_still_playing();
    world_wake_at(t_ok + T_DATA);
    expect_timed_out_with(PLAYBACK_STOP_REASON_NO_DATA, 0U);
}

/**
 * @brief 断流从判据成立起算：播放中输入用尽（窗口不足一帧、无块）后满 T_data 停止 NO_DATA，游标保留。
 */
static void test_starvation_expires_after_t_data(void)
{
    world_reset();
    uint32_t ts = start_starved_playing();

    world_wake_at(ts + T_DATA - 1U);
    expect_still_playing();
    world_wake_at(ts + T_DATA);

    expect_timed_out_with(PLAYBACK_STOP_REASON_NO_DATA, 0U);
}

/**
 * @brief 每收到本流的块重新计时：晚到的块让 T_data 从块到达时重新起算。
 */
static void test_block_arrival_restarts_the_clock(void)
{
    world_reset();
    uint32_t ts = start_starved_playing();
    uint32_t tb = ts + T_DATA - 10U;

    fake_storage_enqueue_block(world_current_seq(), 30U, 1U);
    world_wake_at(tb);
    world_wake_at(tb);   /* 吃完这块，再次断流，从 tb 起算 */
    expect_still_playing();

    world_wake_at(tb + T_DATA - 1U);
    expect_still_playing();
    world_wake_at(tb + T_DATA);
    expect_timed_out_with(PLAYBACK_STOP_REASON_NO_DATA, 0U);
}

/**
 * @brief 块恰好在到期那次唤醒到达：先收块再判定，不误停。
 */
static void test_block_arriving_at_the_expiry_wake_prevents_timeout(void)
{
    world_reset();
    uint32_t ts = start_starved_playing();

    fake_storage_enqueue_block(world_current_seq(), 30U, 1U);
    world_wake_at(ts + T_DATA);

    expect_still_playing();
}

/**
 * @brief 欠载恢复后的补满同属断流：欠载使 T_out 暂停，T_data 照常计时并到期 NO_DATA。
 */
static void test_underrun_refill_starvation_is_timed_by_t_data(void)
{
    world_reset();
    uint32_t ts = start_starved_playing();

    fake_player_emit_output_started();
    world_wake_at(ts + 1U);
    fake_player_emit_underrun();
    world_wake_at(ts + 100U);

    world_wake_at(ts + 100U + T_OUT + 50U);   /* 输出已被 Stop，不是 T_out 的事 */
    expect_still_playing();
    world_wake_at(ts + T_DATA);

    expect_timed_out_with(PLAYBACK_STOP_REASON_NO_DATA, 0U);
}

/**
 * @brief 输出一直有进展时 T_out 不触发，但缺数据达到 T_data 仍停止 NO_DATA（两个计时互不替代）。
 */
static void test_t_data_still_fires_while_output_makes_progress(void)
{
    world_reset();
    uint32_t ts = start_starved_playing();

    fake_player_emit_output_started();
    world_wake_at(ts + 1U);
    for (uint32_t t = ts + 400U; t < ts + T_DATA; t += 400U)
    {
        fake_player_emit_output_progress();
        world_wake_at(t);
        expect_still_playing();
    }
    fake_player_emit_output_progress();
    world_wake_at(ts + T_DATA - 1U);
    expect_still_playing();
    fake_player_emit_output_progress();
    world_wake_at(ts + T_DATA);

    expect_timed_out_with(PLAYBACK_STOP_REASON_NO_DATA, 0U);
}

/**
 * @brief 暂停且输出已满时不计时：暂停任意久都不会因缺数据停止。
 */
static void test_paused_with_full_output_is_not_timed(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);

    world_wake_at(world_now() + (10U * T_DATA));

    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    assert(world_snapshot().StopCount == 0U);
}

/**
 * @brief 暂停中换曲：等开启应答同样计时，到期停止 NO_DATA（暂停不豁免等待应答）。
 */
static void test_paused_track_change_open_wait_is_timed(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_send(PLAYBACK_COMMAND_NEXT);
    uint32_t t0 = world_now();

    world_wake_at(t0 + T_DATA - 1U);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    world_wake_at(t0 + T_DATA);

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_NO_DATA);
    assert(world_snapshot().Index == 1U);
}

/**
 * @brief 暂停中换曲的预填缺数据同样计时：应答之后没有块，满 T_data 停止。
 */
static void test_paused_prefill_starvation_is_timed(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_ok(GENERATION, TRACKS);
    uint32_t t_ok = world_now();

    world_wake_at(t_ok + T_DATA - 1U);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    world_wake_at(t_ok + T_DATA);

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_NO_DATA);
}

/**
 * @brief 排空（流记录 ENDED 之后）不计时：读尽的流不再要数据。
 */
static void test_draining_after_ended_is_not_timed(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_ENDED);
    world_wake();

    world_wake_at(world_now() + (10U * T_DATA));

    expect_still_playing();
}

/**
 * @brief 播完自动前进后，下一首的开启应答等待重新起算 T_data。
 */
static void test_auto_advance_open_wait_is_timed(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_ENDED);
    world_wake();
    fake_player_release_tail();
    world_wake();
    assert(world_snapshot().Index == 1U);
    uint32_t t0 = world_now();

    world_wake_at(t0 + T_DATA - 1U);
    expect_still_playing();
    world_wake_at(t0 + T_DATA);

    expect_timed_out_with(PLAYBACK_STOP_REASON_NO_DATA, 1U);
}

/**
 * @brief 停止后不计时：到期停止之后再久也不会再进入停止。
 */
static void test_stopped_machine_is_never_timed(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_wake_at(world_now() + T_DATA);
    assert(world_snapshot().StopCount == 1U);

    world_wake_at(world_now() + (10U * T_DATA));

    assert(world_snapshot().StopCount == 1U);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_NO_DATA);
}

/**
 * @brief 毫秒时刻回绕不影响判定（按差值比较）。
 */
static void test_timeout_is_wraparound_safe(void)
{
    world_reset();
    world_send_at(PLAYBACK_COMMAND_TOGGLE, UINT32_MAX - 100U);

    world_wake_at(UINT32_MAX - 100U);
    expect_still_playing();
    /* 回绕后的时刻小于起算时刻，world_wake_at 要求单调，这里直接经状态机公开入口步进。 */
    playback_machine_step(world_machine(), NULL, 0U, (UINT32_MAX - 100U) + T_DATA - 1U);
    expect_still_playing();
    playback_machine_step(world_machine(), NULL, 0U, (UINT32_MAX - 100U) + T_DATA);

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_NO_DATA);
}

/* ---------------------------------------------------------------------------
 * T_out
 * ------------------------------------------------------------------------- */

/**
 * @brief 输出启动后 T_out 内没有新的半区释放：到期与 TRANSFER_ERROR 同路，Stop → 停止 AUDIO_ERROR，游标保留。
 */
static void test_output_without_progress_expires_after_t_out(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    fake_player_emit_output_started();
    world_wake();
    uint32_t ts = world_now();

    world_wake_at(ts + T_OUT - 1U);
    expect_still_playing();
    world_wake_at(ts + T_OUT);

    expect_timed_out_with(PLAYBACK_STOP_REASON_AUDIO_ERROR, 0U);
}

/**
 * @brief 每次半区释放重新计时：到期从最近一次释放起算。
 */
static void test_output_progress_restarts_t_out(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    fake_player_emit_output_started();
    world_wake();
    uint32_t ts = world_now();

    fake_player_emit_output_progress();
    world_wake_at(ts + 400U);
    world_wake_at(ts + 400U + T_OUT - 1U);
    expect_still_playing();
    world_wake_at(ts + 400U + T_OUT);

    expect_timed_out_with(PLAYBACK_STOP_REASON_AUDIO_ERROR, 0U);
}

/**
 * @brief 预填阶段（输出尚未 Start）T_out 不计时。
 */
static void test_t_out_does_not_run_before_output_starts(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    fake_storage_enqueue_block(world_current_seq(), 30U, 1U);   /* 一直有数据，只是没有 Start */
    world_wake();

    for (uint32_t i = 1U; i <= 3U; i++)
    {
        fake_storage_enqueue_block(world_current_seq(), 30U, 1U);
        world_wake_at(world_now() + T_OUT);
        expect_still_playing();
    }
}

/**
 * @brief 暂停不计 T_out；恢复（Start）后从恢复时刻起算。
 */
static void test_pause_stops_t_out_and_resume_restarts_it(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    fake_player_emit_output_started();
    world_wake();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);

    world_wake_at(world_now() + (3U * T_OUT));
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);

    world_send(PLAYBACK_COMMAND_TOGGLE);
    uint32_t tr = world_now();
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);

    world_wake_at(tr + T_OUT - 1U);
    expect_still_playing();
    world_wake_at(tr + T_OUT);

    expect_timed_out_with(PLAYBACK_STOP_REASON_AUDIO_ERROR, 0U);
}

/**
 * @brief 运行全部超时测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_open_reply_wait_expires_after_t_data();
    test_open_reply_restarts_the_clock();
    test_starvation_expires_after_t_data();
    test_block_arrival_restarts_the_clock();
    test_block_arriving_at_the_expiry_wake_prevents_timeout();
    test_underrun_refill_starvation_is_timed_by_t_data();
    test_t_data_still_fires_while_output_makes_progress();
    test_paused_with_full_output_is_not_timed();
    test_paused_track_change_open_wait_is_timed();
    test_paused_prefill_starvation_is_timed();
    test_draining_after_ended_is_not_timed();
    test_auto_advance_open_wait_is_timed();
    test_stopped_machine_is_never_timed();
    test_timeout_is_wraparound_safe();
    test_output_without_progress_expires_after_t_out();
    test_output_progress_restarts_t_out();
    test_t_out_does_not_run_before_output_starts();
    test_pause_stops_t_out_and_resume_restarts_it();

    puts("playback_task_timeouts: all tests passed");
    return 0;
}
