/**
 ******************************************************************************
 * @file    test_playback_task_bad_files.c
 * @brief   playback_machine 坏文件跳过与有界停止（#71 B5，以及 B6 里“跳过并提示”的各行）的主机行为测试。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "playback_task_test_support.h"

#define TRACKS 3U
#define GENERATION 7U

/**
 * @brief 当前请求的播放列表位置。
 * @return 位置。
 */
static uint16_t requested_position(void)
{
    return fake_storage_last_request().Position;
}

/**
 * @brief 当前请求是否为“无流”。
 * @return 是为 true。
 */
static bool request_is_no_stream(void)
{
    return fake_storage_last_request().StreamSeq == 0U;
}

/**
 * @brief 应答 BAD_FILE 并确认状态机仍在播放、已按期望位置重新请求。
 * @param[in] expected_position 期望的新请求位置。
 * @param[in] expected_skipped 期望的 SkippedBadCount。
 */
static void bad_file_then_expect_skip_to(uint16_t expected_position, uint32_t expected_skipped)
{
    uint32_t previous_seq = world_current_seq();

    world_open_bad_file();

    assert(world_current_seq() > previous_seq);   /* 每次跳过都是新流序号。 */
    assert(requested_position() == expected_position);
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().Index == expected_position);
    assert(world_snapshot().SkippedBadCount == expected_skipped);
    assert(world_snapshot().StopCount == 0U);
}

/**
 * @brief 在同一次唤醒里让流既有开启 OK 与第一块数据，又带上指定的播放模块事实（首帧与该事实落在同一次推进）。
 * @param[in] fact 要注入的事实。
 * @param[in] tracks 开启应答的曲目数。
 */
static void open_with_data_and_fact(FakePlayerFactTypeDef fact, uint16_t tracks)
{
    fake_storage_enqueue_block(world_current_seq(), 30U, 1U);
    fake_player_set_fact(fact);
    fake_storage_open_ok(world_current_seq(), GENERATION, tracks, WORLD_AUDIO_BYTES);
    world_wake();
}

/**
 * @brief 开启应答 BAD_FILE：跳过并提示（SkippedBadCount +1），沿 NEXT 的方向前进一首，仍报播放、不停止。
 */
static void test_bad_file_skips_forward_after_next(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);

    world_send(PLAYBACK_COMMAND_NEXT);
    assert(requested_position() == 1U);
    bad_file_then_expect_skip_to(2U, 1U);

    assert(world_snapshot().PositionMs == 0U);
    assert(world_snapshot().DurationMs == 0U);
}

/**
 * @brief 跳过方向是最近一次移动的方向：按上一首换到坏文件时继续向前（索引减小）跳。
 */
static void test_bad_file_skips_backward_after_prev(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send_select(GENERATION, 2U);
    world_open_ok(GENERATION, TRACKS);

    world_send(PLAYBACK_COMMAND_PREV);
    assert(requested_position() == 1U);
    bad_file_then_expect_skip_to(0U, 1U);
}

/**
 * @brief 方向随最近一次用户移动更新：PREV 之后再 NEXT，跳过又改回向后。
 */
static void test_direction_follows_most_recent_user_move(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_PREV);   /* 0 → 2 */
    world_send(PLAYBACK_COMMAND_NEXT);   /* 2 → 0 */
    assert(requested_position() == 0U);

    bad_file_then_expect_skip_to(1U, 1U);
}

/**
 * @brief SELECT 与停止态 TOGGLE 的跳过方向都向后（索引增大）。
 */
static void test_select_and_stopped_toggle_skip_forward(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_PREV);   /* 方向向前 */
    world_send_select(GENERATION, 1U);

    bad_file_then_expect_skip_to(2U, 1U);

    world_send(PLAYBACK_COMMAND_PREV);   /* 方向再次向前 */
    fake_storage_open_ok(world_current_seq(), GENERATION, TRACKS, WORLD_AUDIO_BYTES);
    world_wake();
    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_READ_ERROR);
    world_wake();
    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);

    world_send(PLAYBACK_COMMAND_TOGGLE);   /* 停止态 TOGGLE 打开游标曲目，方向向后 */
    uint16_t cursor = world_snapshot().Index;

    world_open_bad_file();
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().Index == (uint16_t)((cursor + 1U) % TRACKS));
    assert(world_snapshot().SkippedBadCount == 2U);
}

/**
 * @brief 整轮都坏：跳过数增加 N，然后停止 ALL_BAD、停止次数 +1，游标保留，请求信箱写成“无流”。
 */
static void test_whole_round_of_bad_files_stops_all_bad(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_NEXT);   /* 位置 1，N = 3 已由开启 OK 带回 */

    world_open_bad_file();   /* 1 */
    world_open_bad_file();   /* 2 */
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    world_open_bad_file();   /* 3 = N */

    Playback_SnapshotTypeDef snapshot = world_snapshot();

    assert(snapshot.State == PLAYBACK_STATE_STOPPED);
    assert(snapshot.StopReason == PLAYBACK_STOP_REASON_ALL_BAD);
    assert(snapshot.SkippedBadCount == TRACKS);
    assert(snapshot.StopCount == 1U);
    assert(snapshot.HasTrack);
    assert(snapshot.Generation == GENERATION);
    assert(snapshot.PositionMs == 0U);
    assert(snapshot.DurationMs == 0U);
    assert(request_is_no_stream());
}

/**
 * @brief N = 1：唯一的曲目是坏文件，跳过数 +1 后立即 ALL_BAD，不原地重试。
 */
static void test_single_track_library_with_bad_file_stops_all_bad(void)
{
    world_reset();
    world_start_playing(GENERATION, 1U);
    uint32_t requests = fake_storage_request_count();

    world_send(PLAYBACK_COMMAND_NEXT);   /* 环形步进回到自己 */
    world_open_bad_file();

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_ALL_BAD);
    assert(world_snapshot().SkippedBadCount == 1U);
    assert(world_snapshot().StopCount == 1U);
    assert(fake_storage_request_count() == requests + 2U);   /* NEXT 的请求与停止时的“无流”，没有重试请求。 */
    assert(request_is_no_stream());
}

/**
 * @brief 连跳计数在首帧解出时清零，而不是在开启 OK 时：坏、坏、OK 但同步预算耗尽，是第三次连续跳过。
 */
static void test_open_ok_does_not_reset_consecutive_count(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_NEXT);

    world_open_bad_file();
    world_open_bad_file();
    fake_player_set_fact(FAKE_FACT_SYNC_EXHAUSTED);
    world_open_ok(GENERATION, TRACKS);   /* 开启 OK，但没有解出首帧 */

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_ALL_BAD);
    assert(world_snapshot().SkippedBadCount == TRACKS);
}

/**
 * @brief 首帧解出后连跳计数清零：此后再坏 N - 1 次仍继续跳过，第 N 次才停止。
 */
static void test_first_frame_resets_consecutive_count(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_bad_file();
    world_open_bad_file();
    world_open_ok(GENERATION, TRACKS);
    world_feed_block(30U);   /* 首帧：计数清零 */
    assert(world_snapshot().DurationMs != 0U);
    assert(world_snapshot().SkippedBadCount == 2U);

    fake_player_set_fact(FAKE_FACT_SYNC_EXHAUSTED);
    world_feed_block(30U);   /* 播放中同步预算耗尽：第 1 次连续跳过 */
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().SkippedBadCount == 3U);
    world_open_bad_file();   /* 第 2 次 */
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().SkippedBadCount == 4U);
    world_open_bad_file();   /* 第 3 次 = N */
    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_ALL_BAD);
    assert(world_snapshot().SkippedBadCount == 5U);
}

/**
 * @brief 用户换曲命令（NEXT、PREV、SELECT、停止态 TOGGLE）把连跳计数清零。
 */
static void test_user_track_commands_reset_consecutive_count(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_bad_file();
    world_open_bad_file();   /* 2 次 */

    world_send(PLAYBACK_COMMAND_PREV);   /* 清零 */
    world_open_bad_file();
    world_open_bad_file();
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);

    world_send_select(GENERATION, 0U);   /* 清零 */
    world_open_bad_file();
    world_open_bad_file();
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);

    world_send(PLAYBACK_COMMAND_NEXT);   /* 清零 */
    world_open_bad_file();
    world_open_bad_file();
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().SkippedBadCount == 8U);
}

/**
 * @brief 进入停止态也清零连跳计数：ALL_BAD 之后再按播放，又可以跳 N - 1 次而不立即停止。
 */
static void test_entering_stopped_resets_consecutive_count(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_bad_file();
    world_open_bad_file();
    world_open_bad_file();
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_ALL_BAD);

    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_open_bad_file();
    world_open_bad_file();

    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_NONE);
    assert(world_snapshot().SkippedBadCount == 5U);
    assert(world_snapshot().StopCount == 1U);
}

/**
 * @brief 暂停中换到坏文件：保持暂停，继续按方向跳过，停在第一首能解出首帧的曲目开头并预填，不出声。
 */
static void test_paused_skips_bad_files_and_stays_paused(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_TOGGLE);   /* 暂停 */
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    uint32_t resumes = call_log_count_of(CALL_RESUME);

    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_bad_file();
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    assert(requested_position() == 2U);

    world_open_ok(GENERATION, TRACKS);
    world_feed_block(30U);

    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    assert(world_snapshot().Index == 2U);
    assert(world_snapshot().SkippedBadCount == 1U);
    assert(world_snapshot().StopCount == 0U);
    assert(world_snapshot().DurationMs != 0U);   /* 暂停中解出首帧，得到总时长 */
    assert(fake_player_is_paused());             /* 预填但不出声 */
    assert(call_log_count_of(CALL_RESUME) == resumes);
}

/**
 * @brief 暂停中整轮都坏：停止 ALL_BAD。
 */
static void test_paused_whole_round_of_bad_files_stops_all_bad(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_send(PLAYBACK_COMMAND_PREV);

    world_open_bad_file();
    world_open_bad_file();
    world_open_bad_file();

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_ALL_BAD);
    assert(world_snapshot().SkippedBadCount == TRACKS);
    assert(world_snapshot().StopCount == 1U);
}

/**
 * @brief 播放模块回报同步预算 R 耗尽：跳过并提示、沿方向前进、计入连跳；旧流被放弃。
 */
static void test_sync_budget_exhausted_skips_like_a_bad_file(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    uint32_t abandons = call_log_count_of(CALL_ABANDON);
    uint32_t seq = world_current_seq();

    fake_player_set_fact(FAKE_FACT_SYNC_EXHAUSTED);
    world_feed_block(30U);

    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().SkippedBadCount == 1U);
    assert(world_snapshot().Index == 1U);
    assert(requested_position() == 1U);
    assert(world_current_seq() > seq);
    assert(call_log_count_of(CALL_ABANDON) == abandons + 1U);
}

/**
 * @brief Platform 因参数拒绝 Start：跳过并提示、沿方向前进、计入连跳。
 */
static void test_start_rejected_skips_like_a_bad_file(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_PREV);
    world_open_ok(GENERATION, TRACKS);

    fake_player_set_fact(FAKE_FACT_START_REJECTED);
    world_feed_block(30U);

    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().SkippedBadCount == 1U);
    assert(world_snapshot().Index == 1U);   /* 0 → PREV → 2 → 向前跳 → 1 */
    assert(world_snapshot().StopCount == 0U);
}

/**
 * @brief 首帧与“Start 被拒”出现在同一次推进里：首帧不清零，否则整库不支持的采样率会无界轮转。
 */
static void test_start_rejection_with_first_frame_still_counts_toward_bound(void)
{
    world_reset();
    world_start_playing(GENERATION, 2U);
    world_send(PLAYBACK_COMMAND_NEXT);

    open_with_data_and_fact(FAKE_FACT_START_REJECTED, 2U);   /* 首帧加 Start 被拒：第 1 次 */
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().SkippedBadCount == 1U);

    open_with_data_and_fact(FAKE_FACT_START_REJECTED, 2U);   /* 第 2 次 = N */

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_ALL_BAD);
    assert(world_snapshot().SkippedBadCount == 2U);
}

/**
 * @brief 曲目数未知（开机后第一首就是坏文件）：无法环形步进与计数，按 NO_TRACK 停止并重置游标。
 */
static void test_bad_file_with_unknown_track_count_stops_no_track(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);

    world_open_bad_file();

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_NO_TRACK);
    assert(!world_snapshot().HasTrack);
    assert(world_snapshot().StopCount == 1U);
    assert(world_snapshot().SkippedBadCount == 0U);
}

/**
 * @brief 坏文件应答若带回代次与曲目数（Storage 已解析出曲目），状态机采用它们，从而可以跳过。
 */
static void test_bad_file_record_carrying_track_count_enables_skipping(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);

    Playback_MachineRecordTypeDef record = { 0 };

    record.StreamSeq = world_current_seq();
    record.OpenResult = PLAYBACK_MACHINE_OPEN_BAD_FILE;
    record.Generation = GENERATION;
    record.TrackCount = TRACKS;
    fake_storage_write_record(&record);
    world_wake();

    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().SkippedBadCount == 1U);
    assert(world_snapshot().Generation == GENERATION);
    assert(world_snapshot().Index == 1U);
    assert(fake_storage_last_request().Generation == GENERATION);
}

/**
 * @brief 旧流的 BAD_FILE 应答被丢弃，不计入跳过。
 */
static void test_stale_bad_file_record_is_ignored(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    uint32_t old_seq = world_current_seq();

    world_send(PLAYBACK_COMMAND_NEXT);
    Playback_MachineRecordTypeDef record = { 0 };

    record.StreamSeq = old_seq;
    record.OpenResult = PLAYBACK_MACHINE_OPEN_BAD_FILE;
    fake_storage_write_record(&record);
    world_wake();

    assert(world_snapshot().SkippedBadCount == 0U);
    assert(world_snapshot().Index == 1U);
}

/**
 * @brief SkippedBadCount 与 StopCount 单调：一轮完整的坏文件、停止、重开、再坏过程中从不下降。
 */
static void test_counters_are_monotonic(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);

    uint32_t skipped = 0U;
    uint32_t stops = 0U;

    for (uint32_t round = 0U; round < 3U; round++)
    {
        world_send(PLAYBACK_COMMAND_NEXT);
        for (uint32_t i = 0U; i < TRACKS; i++)
        {
            world_open_bad_file();
            assert(world_snapshot().SkippedBadCount >= skipped);
            assert(world_snapshot().StopCount >= stops);
            skipped = world_snapshot().SkippedBadCount;
            stops = world_snapshot().StopCount;
        }
        assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
        world_send(PLAYBACK_COMMAND_TOGGLE);
        world_open_ok(GENERATION, TRACKS);
    }
    assert(world_snapshot().SkippedBadCount == 3U * TRACKS);
    assert(world_snapshot().StopCount == 3U);
}

/**
 * @brief 运行全部坏文件测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_bad_file_skips_forward_after_next();
    test_bad_file_skips_backward_after_prev();
    test_direction_follows_most_recent_user_move();
    test_select_and_stopped_toggle_skip_forward();
    test_whole_round_of_bad_files_stops_all_bad();
    test_single_track_library_with_bad_file_stops_all_bad();
    test_open_ok_does_not_reset_consecutive_count();
    test_first_frame_resets_consecutive_count();
    test_user_track_commands_reset_consecutive_count();
    test_entering_stopped_resets_consecutive_count();
    test_paused_skips_bad_files_and_stays_paused();
    test_paused_whole_round_of_bad_files_stops_all_bad();
    test_sync_budget_exhausted_skips_like_a_bad_file();
    test_start_rejected_skips_like_a_bad_file();
    test_start_rejection_with_first_frame_still_counts_toward_bound();
    test_bad_file_with_unknown_track_count_stops_no_track();
    test_bad_file_record_carrying_track_count_enables_skipping();
    test_stale_bad_file_record_is_ignored();
    test_counters_are_monotonic();

    puts("playback_task_bad_files: all tests passed");
    return 0;
}
