/**
 ******************************************************************************
 * @file    test_playback_task_stream_end.c
 * @brief   playback_machine 流尾的主机行为测试：EOF 到播完（B13）、短文件（B14）、自动前进与末曲回首曲、
 *          排空期间与曲间的暂停（B12）。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "playback_task_test_support.h"

#define GENERATION 4U
#define TRACKS 3U

/**
 * @brief 把当前流的字节全部交完：放入块、写 ENDED、唤醒到输入读尽。
 * @param[in] block_bytes 最后一块的字节数。
 */
static void finish_stream_input(uint32_t block_bytes)
{
    fake_storage_enqueue_block(world_current_seq(), block_bytes, 1U);
    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_ENDED);
    world_wake();
}

/**
 * @brief 流记录 ENDED 之后，队列和手中还有本流的块时不告知读尽；全部拷完才告知，且只告知一次。
 */
static void test_finish_input_waits_until_all_blocks_are_copied(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);

    fake_storage_enqueue_block(world_current_seq(), 64U, 0U);   /* 大于窗口，会分几次交入。 */
    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_ENDED);
    world_wake();
    assert(!fake_player_input_finished());
    assert(call_log_count_of(CALL_FINISH_INPUT) == 0U);

    for (uint32_t i = 0U; i < 20U; i++)
    {
        world_wake();
    }

    assert(fake_player_input_finished());
    assert(call_log_count_of(CALL_FINISH_INPUT) == 1U);
    assert(fake_storage_outstanding() == 0U);
}

/**
 * @brief 没有 ENDED 的流不会被告知读尽，即使暂时没有块（断流只是输入侧事实）。
 */
static void test_running_dry_without_ended_does_not_finish_input(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);

    for (uint32_t i = 0U; i < 5U; i++)
    {
        world_wake();
    }

    assert(!fake_player_input_finished());
    assert(call_log_count_of(CALL_FINISH_INPUT) == 0U);
}

/**
 * @brief 播完：流尾放完后游标前进、请求下一首，位置回 0、总时长未知，不出现停止态。
 */
static void test_stream_end_advances_to_next_track_without_stopping(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    uint32_t old_seq = world_current_seq();
    finish_stream_input(20U);
    assert(world_snapshot().PositionMs > 0U);

    fake_player_release_tail();
    world_wake();

    Playback_SnapshotTypeDef snapshot = world_snapshot();

    assert(snapshot.State == PLAYBACK_STATE_PLAYING);
    assert(snapshot.StopReason == PLAYBACK_STOP_REASON_NONE);
    assert(snapshot.HasTrack);
    assert(snapshot.Generation == GENERATION);
    assert(snapshot.Index == 1U);
    assert(snapshot.PositionMs == 0U);
    assert(snapshot.DurationMs == 0U);
    assert(snapshot.StopCount == 0U);
    assert(fake_storage_last_request().StreamSeq > old_seq);
    assert(fake_storage_last_request().Generation == GENERATION);
    assert(fake_storage_last_request().Position == 1U);
}

/**
 * @brief 末曲播完环回首曲，仍在播放，没有停止态。
 */
static void test_last_track_wraps_to_first_after_stream_end(void)
{
    world_reset();
    world_send_select(GENERATION, TRACKS - 1U);
    world_open_ok(GENERATION, TRACKS);
    world_feed_block(30U);
    finish_stream_input(10U);

    fake_player_release_tail();
    world_wake();

    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().Index == 0U);
    assert(world_snapshot().StopCount == 0U);
    assert(fake_storage_last_request().Position == 0U);
}

/**
 * @brief 流尾放完只前进一次：下一首开启期间不再被旧流的“已放完”重复触发。
 */
static void test_stream_end_advances_only_once(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    finish_stream_input(10U);
    fake_player_release_tail();
    world_wake();
    assert(world_snapshot().Index == 1U);
    uint32_t requests = fake_storage_request_count();

    world_wake();
    world_wake();

    assert(world_snapshot().Index == 1U);
    assert(fake_storage_request_count() == requests);
}

/**
 * @brief 播完后下一首照常走“开启 OK → 首帧”，自动前进与手动切歌同一路径（先 Abandon，再重新 BeginStream）。
 */
static void test_next_track_after_stream_end_begins_like_any_switch(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    finish_stream_input(10U);
    fake_player_release_tail();
    world_wake();
    assert(call_log_count_of(CALL_ABANDON) == 1U);

    world_open_ok(GENERATION, TRACKS);
    world_feed_block(30U);

    assert(fake_player_begin_count() == 2U);
    assert(world_snapshot().DurationMs == 180000U);
    assert(world_snapshot().Index == 1U);
    assert(world_snapshot().PositionMs > 0U);
}

/**
 * @brief 短文件：醒来前开启 OK、全部字节和 ENDED 都已就位，一次唤醒内依次处理，不另设路径。
 */
static void test_short_file_fully_read_before_first_wake_is_processed_in_order(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    fake_storage_open_ok(world_current_seq(), GENERATION, TRACKS, 25U);
    fake_storage_enqueue_block(world_current_seq(), 25U, 0U);
    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_ENDED);

    world_wake();

    assert(fake_player_begin_count() == 1U);
    assert(fake_player_last_audio_bytes() == 25U);
    assert(fake_player_fed_total() == 25U);
    assert(fake_player_input_finished());
    assert(world_snapshot().DurationMs == 180000U);
    assert(world_snapshot().PositionMs > 0U);
    assert(fake_storage_outstanding() == 0U);

    fake_player_release_tail();
    world_wake();
    assert(world_snapshot().Index == 1U);
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
}

/**
 * @brief 已解码时间在一首内不回退，切歌后从 0 重新计。
 */
static void test_position_never_decreases_within_a_track_and_resets_on_switch(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_open_ok(GENERATION, TRACKS);

    uint32_t last = 0U;

    for (uint32_t i = 0U; i < 8U; i++)
    {
        fake_storage_enqueue_block(world_current_seq(), 20U, 0U);
        world_wake();
        assert(world_snapshot().PositionMs >= last);
        last = world_snapshot().PositionMs;
    }
    assert(last > 0U);

    world_send(PLAYBACK_COMMAND_NEXT);
    assert(world_snapshot().PositionMs == 0U);
}

/**
 * @brief 排空期间按暂停：不前进；恢复后继续排空，流尾放完才前进。
 */
static void test_pause_during_drain_defers_advance_until_resume(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    finish_stream_input(10U);
    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);

    fake_player_release_tail();
    world_wake();
    assert(world_snapshot().Index == 0U);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);

    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_wake();

    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().Index == 1U);
}

/**
 * @brief 流尾放完与下一首 Start 之间按暂停：下一首照常开启和预填，停在开头保持暂停。
 */
static void test_pause_between_stream_end_and_next_start_holds_next_track_paused(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    finish_stream_input(10U);
    fake_player_release_tail();
    world_wake();
    assert(world_snapshot().Index == 1U);
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);

    world_send(PLAYBACK_COMMAND_TOGGLE);   /* 下一首的开启应答还没到。 */
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);

    world_open_ok(GENERATION, TRACKS);
    world_feed_block(30U);

    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    assert(world_snapshot().Index == 1U);
    assert(fake_player_is_paused());
    assert(world_snapshot().DurationMs == 180000U);
    assert(call_log_count_of(CALL_RESUME) == 0U);
}

/**
 * @brief 运行全部流尾测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_finish_input_waits_until_all_blocks_are_copied();
    test_running_dry_without_ended_does_not_finish_input();
    test_stream_end_advances_to_next_track_without_stopping();
    test_last_track_wraps_to_first_after_stream_end();
    test_stream_end_advances_only_once();
    test_next_track_after_stream_end_begins_like_any_switch();
    test_short_file_fully_read_before_first_wake_is_processed_in_order();
    test_position_never_decreases_within_a_track_and_resets_on_switch();
    test_pause_during_drain_defers_advance_until_resume();
    test_pause_between_stream_end_and_next_start_holds_next_track_paused();

    puts("playback_task_stream_end: all tests passed");
    return 0;
}
