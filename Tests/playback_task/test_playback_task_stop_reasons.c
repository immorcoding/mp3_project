/**
 ******************************************************************************
 * @file    test_playback_task_stop_reasons.c
 * @brief   playback_machine 停止原因（#71 B6 里“停止”的各行、K5 的转换点）的主机行为测试：
 *          开启结果、终止事实与游标保留/重置，以及它们不计入连跳。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "playback_task_test_support.h"

#define TRACKS 4U
#define GENERATION 7U

/**
 * @brief 断言已停止：原因、停止次数、位置与总时长清零，请求信箱写成“无流”，不计入跳过。
 * @param[in] reason 期望的停止原因。
 * @param[in] stop_count 期望的停止次数。
 */
static void expect_stopped(Playback_StopReasonTypeDef reason, uint32_t stop_count)
{
    Playback_SnapshotTypeDef snapshot = world_snapshot();

    assert(snapshot.State == PLAYBACK_STATE_STOPPED);
    assert(snapshot.StopReason == reason);
    assert(snapshot.StopCount == stop_count);
    assert(snapshot.PositionMs == 0U);
    assert(snapshot.DurationMs == 0U);
    assert(snapshot.SkippedBadCount == 0U);
    assert(fake_storage_last_request().StreamSeq == 0U);
}

/**
 * @brief 断言游标保留。
 * @param[in] generation 期望代次。
 * @param[in] index 期望位置。
 */
static void expect_cursor_kept(uint32_t generation, uint16_t index)
{
    assert(world_snapshot().HasTrack);
    assert(world_snapshot().Generation == generation);
    assert(world_snapshot().Index == index);
}

/**
 * @brief 断言游标已重置为无当前曲。
 */
static void expect_cursor_reset(void)
{
    assert(!world_snapshot().HasTrack);
    assert(world_snapshot().Generation == 0U);
    assert(world_snapshot().Index == 0U);
}

/**
 * @brief 开启 IO_ERROR：停止 READ_ERROR，保留游标，不计入连跳（不是坏文件）。
 */
static void test_open_io_error_stops_read_error_and_keeps_cursor(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_NEXT);

    world_open_result(PLAYBACK_MACHINE_OPEN_IO_ERROR);

    expect_stopped(PLAYBACK_STOP_REASON_READ_ERROR, 1U);
    expect_cursor_kept(GENERATION, 1U);
}

/**
 * @brief 开启 NOT_READY：停止 NOT_READY，游标重置为无当前曲。
 */
static void test_open_not_ready_stops_and_resets_cursor(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_NEXT);

    world_open_result(PLAYBACK_MACHINE_OPEN_NOT_READY);

    expect_stopped(PLAYBACK_STOP_REASON_NOT_READY, 1U);
    expect_cursor_reset();
}

/**
 * @brief 开启 STALE：停止 CATALOG_CHANGED，游标重置。
 */
static void test_open_stale_stops_catalog_changed_and_resets_cursor(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send_select(GENERATION, 3U);

    world_open_result(PLAYBACK_MACHINE_OPEN_STALE);

    expect_stopped(PLAYBACK_STOP_REASON_CATALOG_CHANGED, 1U);
    expect_cursor_reset();
}

/**
 * @brief 开启 NO_TRACK（空库或下标越界）：停止 NO_TRACK，游标重置。
 */
static void test_open_no_track_stops_and_resets_cursor(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_NEXT);

    world_open_result(PLAYBACK_MACHINE_OPEN_NO_TRACK);

    expect_stopped(PLAYBACK_STOP_REASON_NO_TRACK, 1U);
    expect_cursor_reset();
}

/**
 * @brief 这些停止都不计入连跳：之后再按播放，坏文件仍要连续 N 个才 ALL_BAD。
 */
static void test_stops_other_than_bad_file_do_not_count_toward_consecutive_skips(void)
{
    world_reset();
    world_start_playing(GENERATION, 2U);
    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_result(PLAYBACK_MACHINE_OPEN_IO_ERROR);

    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_open_bad_file();   /* 连续第 1 个坏文件，N = 2 */

    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().SkippedBadCount == 1U);
}

/**
 * @brief 无卡时按播放：打开 {0, 0} → NOT_READY → 停止，无当前曲，停止次数 +1（K5）；再按播放照常重试。
 */
static void test_play_without_card_stops_not_ready_then_can_retry(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);

    assert(fake_storage_last_request().Generation == 0U);
    assert(fake_storage_last_request().Position == 0U);

    world_open_result(PLAYBACK_MACHINE_OPEN_NOT_READY);

    expect_stopped(PLAYBACK_STOP_REASON_NOT_READY, 1U);
    expect_cursor_reset();

    uint32_t requests = fake_storage_request_count();

    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_NONE);
    assert(fake_storage_request_count() == requests + 1U);
    assert(fake_storage_last_request().Position == 0U);
}

/**
 * @brief 停止后曲目数 N 作废：重置游标之后的 NEXT/PREV 无游标，被忽略。
 */
static void test_next_after_cursor_reset_is_ignored(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_result(PLAYBACK_MACHINE_OPEN_STALE);
    uint32_t requests = fake_storage_request_count();

    world_send(PLAYBACK_COMMAND_NEXT);

    assert(fake_storage_request_count() == requests);
    expect_cursor_reset();
}

/**
 * @brief 流中 READ_ERROR：停止 READ_ERROR，游标保留，播放模块放弃该流、手中块归还。
 */
static void test_stream_read_error_stops_keeping_cursor_and_returns_block(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_ok(GENERATION, TRACKS);
    fake_storage_enqueue_block(world_current_seq(), 64U, 1U);   /* 窗口装不下，手中保持 */
    world_wake();
    uint32_t abandons = call_log_count_of(CALL_ABANDON);

    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_READ_ERROR);
    world_wake();

    expect_stopped(PLAYBACK_STOP_REASON_READ_ERROR, 1U);
    expect_cursor_kept(GENERATION, 1U);
    assert(call_log_count_of(CALL_ABANDON) == abandons + 1U);
    assert(fake_storage_outstanding() == 0U);
    assert(!fake_player_stream_active());
}

/**
 * @brief 拔卡：立即停止 CARD_REMOVED，游标重置，停止次数 +1，放弃流并归还手中块与队列里本流的块。
 */
static void test_card_removed_stops_immediately_and_resets_cursor(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    fake_storage_enqueue_block(world_current_seq(), 64U, 1U);
    world_wake();
    fake_storage_enqueue_block(world_current_seq(), 64U, 1U);
    uint32_t abandons = call_log_count_of(CALL_ABANDON);

    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_CARD_REMOVED);
    world_wake();

    expect_stopped(PLAYBACK_STOP_REASON_CARD_REMOVED, 1U);
    expect_cursor_reset();
    assert(call_log_count_of(CALL_ABANDON) == abandons + 1U);
    assert(fake_storage_outstanding() == 0U);
    assert(fake_storage_ready_count() == 0U);   /* 队列里本流的块随流作废，取出即还 */
}

/**
 * @brief 已读完（ENDED）、本流的块仍在队列里（从内存播放）时拔卡：同一流序号的记录变为 CARD_REMOVED，
 *        立即停止，无当前曲，本流的块全部归还，停止次数 +1；暂停中同样成立。
 */
static void test_card_removed_after_ended_stops_immediately(void)
{
    for (uint32_t pause = 0U; pause <= 1U; pause++)
    {
        world_reset();
        world_start_playing(GENERATION, TRACKS);
        fake_storage_enqueue_block(world_current_seq(), 64U, 1U);
        fake_storage_enqueue_block(world_current_seq(), 64U, 1U);
        fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_ENDED);
        world_wake();
        if (pause != 0U)
        {
            world_send(PLAYBACK_COMMAND_TOGGLE);
            assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
        }
        assert(world_snapshot().State != PLAYBACK_STATE_STOPPED);

        fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_CARD_REMOVED);
        world_wake();

        expect_stopped(PLAYBACK_STOP_REASON_CARD_REMOVED, 1U);
        assert(!world_snapshot().HasTrack);
        assert(fake_storage_outstanding() == 0U);
        assert(fake_storage_ready_count() == 0U);
    }
}

/**
 * @brief 开启应答之前就收到拔卡（消抖后以 CARD_REMOVED 收尾）：同样停止并重置游标。
 */
static void test_card_removed_before_open_reply_stops(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);

    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_CARD_REMOVED);
    world_wake();

    expect_stopped(PLAYBACK_STOP_REASON_CARD_REMOVED, 1U);
    expect_cursor_reset();
}

/**
 * @brief 拔卡发生在暂停中也立即停止；重新插卡后不续播，按播放才从头打开（无游标按 {0, 0}）。
 */
static void test_card_removed_while_paused_stops_and_does_not_resume(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);

    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_CARD_REMOVED);
    world_wake();
    expect_stopped(PLAYBACK_STOP_REASON_CARD_REMOVED, 1U);

    world_wake();   /* 新插卡并不使状态机自己开流 */
    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);

    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(fake_storage_last_request().Generation == 0U);
    assert(fake_storage_last_request().Position == 0U);
}

/**
 * @brief 旧流的拔卡/读错误事实被丢弃：重选之后旧序号的终止状态不能停掉新流。
 */
static void test_stale_stream_end_facts_are_ignored(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    uint32_t old_seq = world_current_seq();

    world_send(PLAYBACK_COMMAND_NEXT);
    fake_storage_set_end(old_seq, PLAYBACK_MACHINE_END_CARD_REMOVED);
    world_wake();

    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().StopCount == 0U);
}

/**
 * @brief 停止的 K5 快照：停止次数每次进入停止 +1，从不下降；再开播后原因清为 NONE。
 */
static void test_stop_count_increments_per_stop_and_reason_clears_on_play(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_READ_ERROR);
    world_wake();
    assert(world_snapshot().StopCount == 1U);

    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_NONE);
    assert(world_snapshot().StopCount == 1U);

    world_open_result(PLAYBACK_MACHINE_OPEN_IO_ERROR);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_READ_ERROR);
    assert(world_snapshot().StopCount == 2U);
}

/**
 * @brief 运行全部停止原因测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_open_io_error_stops_read_error_and_keeps_cursor();
    test_open_not_ready_stops_and_resets_cursor();
    test_open_stale_stops_catalog_changed_and_resets_cursor();
    test_open_no_track_stops_and_resets_cursor();
    test_stops_other_than_bad_file_do_not_count_toward_consecutive_skips();
    test_play_without_card_stops_not_ready_then_can_retry();
    test_next_after_cursor_reset_is_ignored();
    test_stream_read_error_stops_keeping_cursor_and_returns_block();
    test_card_removed_stops_immediately_and_resets_cursor();
    test_card_removed_after_ended_stops_immediately();
    test_card_removed_before_open_reply_stops();
    test_card_removed_while_paused_stops_and_does_not_resume();
    test_stale_stream_end_facts_are_ignored();
    test_stop_count_increments_per_stop_and_reason_clears_on_play();

    puts("playback_task_stop_reasons: all tests passed");
    return 0;
}
