/**
 ******************************************************************************
 * @file    test_playback_task_seek.c
 * @brief   playback_machine 跳转（SEEK）的主机行为测试（#176，#172 C/D/F 节，#166）：执行与落点、三种静默忽略、
 *          最新者胜、带偏移重开流、暂停中预填、越尾、TrackStartCount、作废、计时与 Seek INFO。
 * @note    假播放模块的线性映射：音频区间 4096 B、总时长 180000 ms，所以 45000 / 90000 / 135000 ms 对应偏移
 *          1024 / 2048 / 3072 B 且落点等于目标；越尾钳到 4096 - 20 = 4076 B，落点 179121 ms。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "playback_task_test_support.h"

#define GEN 7U
#define TRACKS 5U

/**
 * @brief 复位并从停止态开播到首帧解出（曲目 {GEN, 0}，总时长 180000 ms，TrackStartCount 为 1）。
 */
static void start_playing(void)
{
    world_reset();
    world_start_playing(GEN, TRACKS);
    assert(world_snapshot().DurationMs == 180000U);
    assert(world_snapshot().TrackStartCount == 1U);
}

/**
 * @brief 日志里以 "Seek " 开头的 INFO 条数。
 * @return 条数。
 */
static uint32_t count_seek_logs(void)
{
    uint32_t count = 0U;

    for (uint32_t i = 0U; i < fake_log_count(); i++)
    {
        if (strncmp(fake_log_text(i), "Seek ", 5U) == 0)
        {
            count++;
        }
    }
    return count;
}

/**
 * @brief 最后一条 INFO 的文本。
 * @return 文本。
 */
static const char *last_log(void)
{
    assert(fake_log_count() > 0U);
    return fake_log_text(fake_log_count() - 1U);
}

/**
 * @brief 把跳转后的流跑到输出起播：Storage 应答开启 OK，放入首块，播放模块回报 OutputStarted。
 */
static void complete_playing_seek(void)
{
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);
    fake_player_emit_output_started();
    world_wake();
}

/**
 * @brief 让当前流读尽并放完，触发播完前进。
 */
static void finish_track_and_advance(void)
{
    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_ENDED);
    for (uint32_t i = 0U; i < 4U; i++)
    {
        world_wake();
    }
    fake_player_release_tail();
    world_wake();
}

/**
 * @brief 播放中跳转：同一步里落点写进快照并回显序号，开新流带偏移，B11 顺序，不改三态、不计停止、不清总时长。
 */
static void test_seek_while_playing_sets_landing_and_reopens_stream_with_offset(void)
{
    start_playing();
    const uint32_t old_seq = world_current_seq();
    const uint32_t requests = fake_storage_request_count();

    world_send_seek(GEN, 0U, 90000U);

    Playback_SnapshotTypeDef snap = world_snapshot();
    assert(snap.State == PLAYBACK_STATE_PLAYING);
    assert(snap.PositionMs == 90000U);
    assert(snap.DurationMs == 180000U);
    assert(snap.HandledCommandSeq == world_last_command_seq());
    assert(snap.StopCount == 0U);
    assert(snap.TrackStartCount == 1U);
    assert(snap.HasTrack && (snap.Generation == GEN) && (snap.Index == 0U));

    assert(fake_storage_request_count() == requests + 1U);
    Playback_MachineRequestTypeDef request = fake_storage_last_request();
    assert(request.StreamSeq == old_seq + 1U);
    assert(request.AudioOffset == 2048U);
    assert(request.Generation == GEN);
    assert(request.Position == 0U);

    /* B11：写请求在放弃旧流之前；新流还没开始，播放模块此刻不重开。 */
    assert(call_log_index_of(CALL_WRITE_REQUEST, fake_storage_request_count() - 1U) <
           call_log_index_of(CALL_ABANDON, 0U));
    assert(fake_player_begin_count() == 1U);
    assert(fake_player_reopen_count() == 0U);

    const uint32_t logs_before = fake_log_count();

    world_open_ok(GEN, TRACKS);
    assert(fake_player_reopen_count() == 1U);
    assert(fake_player_last_reopen_landing_ms() == 90000U);
    assert(fake_player_begin_count() == 1U);   /* 重开不是新的 BeginStream。 */
    assert(world_snapshot().PositionMs == 90000U);

    world_feed_block(30U);
    assert(world_snapshot().PositionMs == 90052U);   /* 新流内照旧只增不减，从落点起算。 */
    assert(world_snapshot().DurationMs == 180000U);
    assert(fake_log_count() == logs_before);          /* 重开不再回报首帧，所以没有第二条 Open；Seek 等起播。 */
}

/**
 * @brief 输出重新起播时记一条 Seek INFO：字段、不含文件名，lat 为处理 SEEK 到起播的 DWT 差值。
 */
static void test_seek_info_is_logged_when_output_restarts_with_dwt_latency(void)
{
    start_playing();

    fake_cycles_advance(123456U);   /* 绝对读数无关紧要，lat 只取差值。 */
    world_send_seek(GEN, 0U, 90000U);
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);
    assert(count_seek_logs() == 0U);   /* 输出还没起播：不记意图。 */

    fake_cycles_advance(8800000U);     /* 100 个周期 1 微秒 -> 88000 us */
    fake_player_emit_output_started();
    world_wake();

    assert(count_seek_logs() == 1U);
    assert(strcmp(last_log(), "Seek idx=0 tgt=90000 land=90000 off=2048 map=lin lat=88000") == 0);

    for (uint32_t i = 0U; i < 3U; i++)
    {
        world_wake();
    }
    assert(count_seek_logs() == 1U);   /* 只记一次。 */
}

/**
 * @brief TOC 映射写 map=toc，钳位写 clamp。
 */
static void test_seek_info_names_toc_map_and_clamp(void)
{
    start_playing();
    fake_player_set_map_kind(SERVICE_PLAYBACK_SEEK_MAP_TOC);
    world_send_seek(GEN, 0U, 180000U);
    complete_playing_seek();

    assert(strcmp(last_log(), "Seek idx=0 tgt=180000 land=179121 off=4076 map=toc clamp lat=0") == 0);
}

/**
 * @brief 三种静默忽略：无当前曲、曲目不符、总时长 0；以及停止态。都只记 DEBUG、推进序号、不动三态与停止计数、不写请求。
 */
static void test_ignored_seeks_only_debug_log_and_advance_the_echo(void)
{
    /* (a) 没有当前曲。 */
    world_reset();
    world_send_seek(1U, 0U, 1000U);
    assert(world_snapshot().HandledCommandSeq == 1U);
    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopCount == 0U);
    assert(fake_storage_request_count() == 0U);
    assert(fake_debug_count() == 1U);
    assert(fake_log_count() == 0U);
    assert(fake_player_map_count() == 0U);

    /* (b) 曲目不符：代次不符，位置不符。 */
    start_playing();
    uint32_t requests = fake_storage_request_count();
    fake_log_clear();
    world_send_seek(GEN + 1U, 0U, 90000U);
    world_send_seek(GEN, 3U, 90000U);
    assert(world_snapshot().HandledCommandSeq == world_last_command_seq());
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().PositionMs < 1000U);   /* 没有跳到目标（旧流仍在自然推进）。 */
    assert(world_snapshot().StopCount == 0U);
    assert(fake_storage_request_count() == requests);
    assert(fake_debug_count() == 2U);
    assert(fake_log_count() == 0U);

    /* (c) 总时长 0：开流等待中、首帧之前。 */
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_open_ok(GEN, TRACKS);
    assert(world_snapshot().DurationMs == 0U);
    requests = fake_storage_request_count();
    fake_log_clear();
    world_send_seek(GEN, 0U, 90000U);
    assert(world_snapshot().HandledCommandSeq == world_last_command_seq());
    assert(fake_storage_request_count() == requests);
    assert(fake_debug_count() == 1U);
    assert(fake_log_count() == 0U);

    /* (d) 停止态（游标保留）。 */
    start_playing();
    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_result(PLAYBACK_MACHINE_OPEN_IO_ERROR);
    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().HasTrack);
    const uint32_t stops = world_snapshot().StopCount;
    requests = fake_storage_request_count();
    fake_log_clear();
    world_send_seek(GEN, 1U, 90000U);
    assert(world_snapshot().HandledCommandSeq == world_last_command_seq());
    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopCount == stops);
    assert(fake_storage_request_count() == requests);
    assert(fake_debug_count() == 1U);
    assert(fake_log_count() == 0U);
}

/**
 * @brief 映射失败（播放模块没有可用的流级事实）同样按忽略处理：不开流、推进序号。
 */
static void test_seek_is_ignored_when_the_service_cannot_map(void)
{
    start_playing();
    const uint32_t requests = fake_storage_request_count();

    fake_player_fail_next_map();
    world_send_seek(GEN, 0U, 90000U);

    assert(world_snapshot().HandledCommandSeq == world_last_command_seq());
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(fake_storage_request_count() == requests);
    assert(fake_debug_count() == 1U);
    assert(fake_player_stream_active());   /* 旧流原样继续。 */
}

/**
 * @brief 同批连续 SEEK 只执行最后一条，被合并的也推进序号，且不记 INFO、不记 DEBUG。
 */
static void test_consecutive_seeks_in_one_batch_execute_only_the_last(void)
{
    start_playing();
    const uint32_t requests = fake_storage_request_count();
    const uint32_t old_seq = world_current_seq();
    Playback_CommandTypeDef commands[3];

    commands[0] = world_make_seek(GEN, 0U, 45000U);
    commands[1] = world_make_seek(GEN, 0U, 90000U);
    commands[2] = world_make_seek(GEN, 0U, 135000U);
    fake_log_clear();
    world_step_commands(commands, 3U);

    assert(world_snapshot().HandledCommandSeq == commands[2].Seq);
    assert(fake_player_map_count() == 1U);
    assert(fake_storage_request_count() == requests + 1U);
    assert(fake_storage_last_request().StreamSeq == old_seq + 1U);
    assert(fake_storage_last_request().AudioOffset == 3072U);
    assert(world_snapshot().PositionMs == 135000U);
    assert(fake_debug_count() == 0U);
    assert(fake_log_count() == 0U);

    complete_playing_seek();
    assert(count_seek_logs() == 1U);
    assert(strcmp(last_log(), "Seek idx=0 tgt=135000 land=135000 off=3072 map=lin lat=0") == 0);
}

/**
 * @brief 不同类型的命令不合并：SEEK、TOGGLE、SEEK 两次跳转都执行。
 */
static void test_seeks_separated_by_another_command_are_not_merged(void)
{
    start_playing();
    const uint32_t requests = fake_storage_request_count();
    Playback_CommandTypeDef commands[3];

    commands[0] = world_make_seek(GEN, 0U, 45000U);
    commands[1] = world_make_command(PLAYBACK_COMMAND_TOGGLE);
    commands[2] = world_make_seek(GEN, 0U, 90000U);
    world_step_commands(commands, 3U);

    assert(fake_player_map_count() == 2U);
    assert(fake_storage_request_count() == requests + 2U);
    assert(fake_storage_last_request().AudioOffset == 2048U);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    assert(world_snapshot().PositionMs == 90000U);
    assert(world_snapshot().HandledCommandSeq == commands[2].Seq);
}

/**
 * @brief 跨唤醒的新跳转开新 StreamSeq，旧跳转作废：旧流的应答与块被丢弃，只重开一次，只记一条 Seek INFO。
 */
static void test_a_newer_seek_in_a_later_wake_voids_the_pending_one(void)
{
    start_playing();
    world_send_seek(GEN, 0U, 45000U);
    const uint32_t first_seq = world_current_seq();

    fake_storage_enqueue_block(first_seq, 20U, 1U);
    world_send_seek(GEN, 0U, 135000U);
    const uint32_t second_seq = world_current_seq();

    assert(second_seq == first_seq + 1U);
    assert(fake_storage_last_request().AudioOffset == 3072U);
    assert(fake_storage_ready_count() == 0U);        /* 旧跳转流的块取出即归还。 */
    assert(fake_storage_outstanding() == 0U);

    fake_storage_open_ok(first_seq, GEN, TRACKS, WORLD_AUDIO_BYTES);   /* 迟到的旧应答。 */
    world_wake();
    assert(fake_player_reopen_count() == 0U);

    complete_playing_seek();
    assert(fake_player_reopen_count() == 1U);
    assert(fake_player_last_reopen_landing_ms() == 135000U);
    assert(count_seek_logs() == 1U);
    assert(strstr(last_log(), "tgt=135000") != NULL);
}

/**
 * @brief 跳转时旧流已读出的块被丢弃。
 */
static void test_seek_discards_blocks_of_the_old_stream(void)
{
    start_playing();
    fake_storage_enqueue_block(world_current_seq(), 20U, 1U);

    world_send_seek(GEN, 0U, 90000U);

    assert(fake_storage_ready_count() == 0U);
    assert(fake_storage_outstanding() == 0U);
    assert(fake_storage_returned_count() >= 1U);
}

/**
 * @brief 跳转之后到新流开始之前不接受旧流的位置：位置保持落点（含向回跳变小）；新流内照旧只增不减。
 */
static void test_position_stays_at_the_landing_until_the_new_stream_runs(void)
{
    start_playing();
    world_send_seek(GEN, 0U, 135000U);
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);
    assert(world_snapshot().PositionMs == 135052U);

    world_send_seek(GEN, 0U, 45000U);   /* 位置变小。 */
    assert(world_snapshot().PositionMs == 45000U);
    for (uint32_t i = 0U; i < 3U; i++)
    {
        world_wake();
        assert(world_snapshot().PositionMs == 45000U);
    }
    world_open_ok(GEN, TRACKS);
    assert(world_snapshot().PositionMs == 45000U);
    world_feed_block(30U);
    assert(world_snapshot().PositionMs == 45052U);
}

/**
 * @brief 暂停中跳转：保持 PAUSED，落点进快照，重开后立即暂停输出（B12 预填），预填完成时记 Seek INFO，不出声。
 */
static void test_seek_while_paused_keeps_paused_and_logs_when_prefill_completes(void)
{
    start_playing();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    fake_log_clear();

    fake_cycles_advance(777U);
    world_send_seek(GEN, 0U, 90000U);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    assert(world_snapshot().PositionMs == 90000U);
    assert(world_snapshot().StopCount == 0U);

    world_open_ok(GEN, TRACKS);
    assert(fake_player_reopen_count() == 1U);
    assert(fake_player_is_paused());
    assert(call_log_index_of(CALL_PAUSE, call_log_count_of(CALL_PAUSE) - 1U) >
           call_log_index_of(CALL_REOPEN_STREAM, 0U));
    assert(count_seek_logs() == 0U);   /* 预填还没完成。 */

    fake_cycles_advance(5000000U);
    world_feed_block(30U);
    assert(count_seek_logs() == 1U);
    assert(strcmp(last_log(), "Seek idx=0 tgt=90000 land=90000 off=2048 map=lin lat=50000") == 0);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    assert(fake_player_is_paused());
    assert(call_log_count_of(CALL_RESUME) == 0U);

    world_wake();
    assert(count_seek_logs() == 1U);

    world_send(PLAYBACK_COMMAND_TOGGLE);   /* 按播放从落点出声。 */
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().PositionMs >= 90000U);
}

/**
 * @brief 越尾：钳位后照常读完、播完、前进到下一首，前进时 TrackStartCount 加 1。
 */
static void test_seek_past_the_end_plays_out_and_advances(void)
{
    start_playing();
    world_send_seek(GEN, 0U, 180000U);
    assert(fake_storage_last_request().AudioOffset == 4076U);
    assert(world_snapshot().PositionMs == 179121U);
    complete_playing_seek();

    finish_track_and_advance();
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().Index == 1U);
    assert(world_snapshot().TrackStartCount == 2U);
    assert(world_snapshot().StopCount == 0U);
    assert(strcmp(last_log(), "Auto idx=1") == 0);
}

/**
 * @brief 暂停中越尾：停在末尾不前进，恢复后放完再前进。
 */
static void test_seek_past_the_end_while_paused_stops_at_the_end_until_resumed(void)
{
    start_playing();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_send_seek(GEN, 0U, 180000U);
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);

    finish_track_and_advance();
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    assert(world_snapshot().Index == 0U);
    assert(world_snapshot().PositionMs >= 179121U);

    world_send(PLAYBACK_COMMAND_TOGGLE);
    for (uint32_t i = 0U; i < 4U; i++)
    {
        world_wake();
    }
    fake_player_release_tail();
    world_wake();
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().Index == 1U);
    assert(world_snapshot().TrackStartCount == 2U);
}

/**
 * @brief TrackStartCount：SELECT（含同曲重选）、NEXT、PREV、播完前进、跳过坏文件各加 1；跳转（含跳到 0）不加。
 */
static void test_track_start_count_counts_track_starts_but_not_seeks(void)
{
    start_playing();
    assert(world_snapshot().TrackStartCount == 1U);

    world_send_seek(GEN, 0U, 0U);   /* 跳到 0：普通跳转，不是同曲重选。 */
    assert(world_snapshot().PositionMs == 0U);
    assert(world_snapshot().TrackStartCount == 1U);
    complete_playing_seek();
    assert(world_snapshot().TrackStartCount == 1U);

    world_send_select(GEN, 0U);     /* 同曲重选。 */
    assert(world_snapshot().TrackStartCount == 2U);
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);

    world_send(PLAYBACK_COMMAND_NEXT);
    assert(world_snapshot().TrackStartCount == 3U);
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);

    world_send(PLAYBACK_COMMAND_PREV);
    assert(world_snapshot().TrackStartCount == 4U);
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);

    finish_track_and_advance();         /* 播完前进。 */
    assert(world_snapshot().TrackStartCount == 5U);
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);

    world_send(PLAYBACK_COMMAND_NEXT);  /* 用户切歌加 1，开出来是坏文件，跳过后开下一首再加 1。 */
    assert(world_snapshot().TrackStartCount == 6U);
    world_open_bad_file();
    assert(world_snapshot().SkippedBadCount == 1U);
    assert(world_snapshot().TrackStartCount == 7U);
}

/**
 * @brief 跳转未完成时切歌命令到来：跳转作废，新流不带偏移，走 BeginStream 而非重开，永远没有 Seek INFO。
 */
static void test_next_after_seek_voids_the_pending_seek(void)
{
    start_playing();
    world_send_seek(GEN, 0U, 90000U);
    world_send(PLAYBACK_COMMAND_NEXT);

    assert(fake_storage_last_request().AudioOffset == 0U);
    assert(fake_storage_last_request().Position == 1U);
    assert(world_snapshot().PositionMs == 0U);
    assert(world_snapshot().DurationMs == 0U);

    world_open_ok(GEN, TRACKS);
    assert(fake_player_reopen_count() == 0U);
    assert(fake_player_begin_count() == 2U);
    world_feed_block(30U);
    fake_player_emit_output_started();
    world_wake();
    assert(count_seek_logs() == 0U);
}

/**
 * @brief 重开已完成、尚未起播时切歌：Seek INFO 同样作废。
 */
static void test_next_after_reopen_but_before_output_voids_the_seek_log(void)
{
    start_playing();
    world_send_seek(GEN, 0U, 90000U);
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);
    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);
    fake_player_emit_output_started();
    world_wake();

    assert(count_seek_logs() == 0U);
}

/**
 * @brief 进入停止态时未完成的跳转作废。
 */
static void test_stop_voids_the_pending_seek(void)
{
    start_playing();
    world_send_seek(GEN, 0U, 90000U);
    world_open_result(PLAYBACK_MACHINE_OPEN_IO_ERROR);
    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);

    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);
    fake_player_emit_output_started();
    world_wake();
    assert(count_seek_logs() == 0U);
    assert(fake_player_reopen_count() == 0U);
}

/**
 * @brief 普通开流（切歌、SELECT、停止态开播）的 AudioOffset 都是 0。
 */
static void test_ordinary_opens_request_zero_offset(void)
{
    start_playing();
    assert(fake_storage_last_request().AudioOffset == 0U);

    world_send_seek(GEN, 0U, 90000U);
    assert(fake_storage_last_request().AudioOffset == 2048U);
    world_send(PLAYBACK_COMMAND_NEXT);
    assert(fake_storage_last_request().AudioOffset == 0U);

    world_send_seek(GEN, 1U, 90000U);   /* 切歌后总时长未知：被忽略，信箱不变。 */
    assert(fake_storage_last_request().AudioOffset == 0U);

    world_send_select(GEN, 2U);
    assert(fake_storage_last_request().AudioOffset == 0U);
}

/**
 * @brief T_data 与 T_out 与切歌一致：放弃旧流时停 T_out；等应答从写请求起算，到期按 NO_DATA 停止。
 */
static void test_timeouts_behave_like_a_track_switch(void)
{
    start_playing();
    fake_player_emit_output_started();
    world_wake();                               /* T_out 从此刻起算。 */

    world_send_seek(GEN, 0U, 90000U);
    const uint32_t t_seek = world_now();

    world_wake_at(t_seek + 600U);               /* 超过 T_out，但放弃旧流时 T_out 已停。 */
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    world_wake_at(t_seek + 1990U);
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    world_wake_at(t_seek + 2000U);
    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_NO_DATA);
    assert(world_snapshot().StopCount == 1U);
    assert(count_seek_logs() == 0U);
}

/**
 * @brief 起播后 T_out 重新计时：跳转后的 OutputStarted 起算，之后按常规到期。
 */
static void test_output_timeout_restarts_with_the_restart_of_output(void)
{
    start_playing();
    world_send_seek(GEN, 0U, 90000U);
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);
    fake_player_emit_output_started();
    world_wake();
    const uint32_t t_started = world_now();

    world_wake_at(t_started + 490U);
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    world_wake_at(t_started + 500U);
    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_AUDIO_ERROR);
}

/**
 * @brief 放弃旧流时 Stop 失败：与切歌一致，停止 AUDIO_FAULT 并锁存。
 */
static void test_stop_failure_while_seeking_latches_audio_fault(void)
{
    start_playing();
    fake_player_fail_next_stop();
    world_send_seek(GEN, 0U, 90000U);

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_AUDIO_FAULT);
    assert(world_snapshot().HandledCommandSeq == world_last_command_seq());
    assert(count_seek_logs() == 0U);
}

/**
 * @brief 重开失败按 AUDIO_FAULT 停止（与 BeginStream 被拒一致）。
 */
static void test_reopen_failure_stops_with_audio_fault(void)
{
    start_playing();
    world_send_seek(GEN, 0U, 90000U);
    fake_player_fail_next_reopen();
    world_open_ok(GEN, TRACKS);

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_AUDIO_FAULT);
    assert(count_seek_logs() == 0U);
}

/**
 * @brief 运行全部跳转测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_seek_while_playing_sets_landing_and_reopens_stream_with_offset();
    test_seek_info_is_logged_when_output_restarts_with_dwt_latency();
    test_seek_info_names_toc_map_and_clamp();
    test_ignored_seeks_only_debug_log_and_advance_the_echo();
    test_seek_is_ignored_when_the_service_cannot_map();
    test_consecutive_seeks_in_one_batch_execute_only_the_last();
    test_seeks_separated_by_another_command_are_not_merged();
    test_a_newer_seek_in_a_later_wake_voids_the_pending_one();
    test_seek_discards_blocks_of_the_old_stream();
    test_position_stays_at_the_landing_until_the_new_stream_runs();
    test_seek_while_paused_keeps_paused_and_logs_when_prefill_completes();
    test_seek_past_the_end_plays_out_and_advances();
    test_seek_past_the_end_while_paused_stops_at_the_end_until_resumed();
    test_track_start_count_counts_track_starts_but_not_seeks();
    test_next_after_seek_voids_the_pending_seek();
    test_next_after_reopen_but_before_output_voids_the_seek_log();
    test_stop_voids_the_pending_seek();
    test_ordinary_opens_request_zero_offset();
    test_timeouts_behave_like_a_track_switch();
    test_output_timeout_restarts_with_the_restart_of_output();
    test_stop_failure_while_seeking_latches_audio_fault();
    test_reopen_failure_stops_with_audio_fault();

    puts("playback_task_seek: all tests passed");
    return 0;
}
