/**
 ******************************************************************************
 * @file    test_playback_task_faults.c
 * @brief   playback_machine 的音频故障（#71 B6 的 Platform 各行、B10）与 K3 的 AUDIO_FAULT 锁存规则的主机行为测试：
 *          TRANSFER_ERROR 可重试，STOP_FAILED 锁存后 TOGGLE/SELECT 只让停止次数加一，NEXT/PREV 只移动游标。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "playback_task_test_support.h"

#define TRACKS 4U
#define GENERATION 7U

/**
 * @brief 把状态机带到“AUDIO_FAULT 已锁存”：播放第 1 首（位置 1）时 Platform 回报 STOP_FAILED。
 */
static void latch_audio_fault(void)
{
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_ok(GENERATION, TRACKS);
    fake_player_set_fact(FAKE_FACT_STOP_FAILED);
    world_wake();

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_AUDIO_FAULT);
}

/**
 * @brief TRANSFER_ERROR：Stop → 停止 AUDIO_ERROR，游标保留，不计入连跳；再按播放就是重试（可恢复，不锁存）。
 */
static void test_transfer_error_stops_audio_error_and_can_be_retried(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_ok(GENERATION, TRACKS);
    uint32_t abandons = call_log_count_of(CALL_ABANDON);

    fake_player_set_fact(FAKE_FACT_TRANSFER_ERROR);
    world_wake();

    Playback_SnapshotTypeDef snapshot = world_snapshot();

    assert(snapshot.State == PLAYBACK_STATE_STOPPED);
    assert(snapshot.StopReason == PLAYBACK_STOP_REASON_AUDIO_ERROR);
    assert(snapshot.StopCount == 1U);
    assert(snapshot.SkippedBadCount == 0U);
    assert(snapshot.HasTrack);
    assert(snapshot.Index == 1U);
    assert(fake_storage_last_request().StreamSeq == 0U);
    assert(call_log_count_of(CALL_ABANDON) == abandons + 1U);

    world_send(PLAYBACK_COMMAND_TOGGLE);   /* 重试：打开游标所指的同一首 */
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_NONE);
    assert(fake_storage_last_request().StreamSeq != 0U);
    assert(fake_storage_last_request().Position == 1U);
}

/**
 * @brief 播放模块推进本身失败（读状态或提交半区失败）按音频错误处理，同样可重试。
 */
static void test_pump_failure_stops_audio_error(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);

    fake_player_fail_next_pump();
    world_wake();

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_AUDIO_ERROR);
    assert(world_snapshot().StopCount == 1U);
    assert(world_snapshot().HasTrack);
}

/**
 * @brief STOP_FAILED：写“无流”、保持静音、停止 AUDIO_FAULT，游标保留，不计入连跳。
 */
static void test_stop_failed_stops_audio_fault_and_keeps_cursor(void)
{
    world_reset();
    latch_audio_fault();

    Playback_SnapshotTypeDef snapshot = world_snapshot();

    assert(snapshot.StopCount == 1U);
    assert(snapshot.SkippedBadCount == 0U);
    assert(snapshot.HasTrack);
    assert(snapshot.Generation == GENERATION);
    assert(snapshot.Index == 1U);
    assert(fake_storage_last_request().StreamSeq == 0U);
}

/**
 * @brief 锁存后 TOGGLE：不开流、不出声，游标不动，只让停止次数 +1 以重新给出原因，命令序号照常回显。
 */
static void test_latched_toggle_only_bumps_stop_count(void)
{
    world_reset();
    latch_audio_fault();
    uint32_t requests = fake_storage_request_count();
    uint32_t begins = fake_player_begin_count();

    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_send(PLAYBACK_COMMAND_TOGGLE);

    Playback_SnapshotTypeDef snapshot = world_snapshot();

    assert(snapshot.State == PLAYBACK_STATE_STOPPED);
    assert(snapshot.StopReason == PLAYBACK_STOP_REASON_AUDIO_FAULT);
    assert(snapshot.StopCount == 3U);
    assert(snapshot.Index == 1U);
    assert(snapshot.Generation == GENERATION);
    assert(snapshot.HandledCommandSeq == world_last_command_seq());
    assert(fake_storage_request_count() == requests);
    assert(fake_player_begin_count() == begins);
}

/**
 * @brief 锁存后 SELECT：同样只让停止次数 +1，游标不动、不开流。
 */
static void test_latched_select_only_bumps_stop_count(void)
{
    world_reset();
    latch_audio_fault();
    uint32_t requests = fake_storage_request_count();

    world_send_select(GENERATION, 3U);

    Playback_SnapshotTypeDef snapshot = world_snapshot();

    assert(snapshot.State == PLAYBACK_STATE_STOPPED);
    assert(snapshot.StopReason == PLAYBACK_STOP_REASON_AUDIO_FAULT);
    assert(snapshot.StopCount == 2U);
    assert(snapshot.Index == 1U);
    assert(snapshot.HandledCommandSeq == world_last_command_seq());
    assert(fake_storage_request_count() == requests);
}

/**
 * @brief 锁存后 NEXT/PREV 按停止态规则只移动游标：不开流，停止次数与原因不变。
 */
static void test_latched_next_and_prev_only_move_the_cursor(void)
{
    world_reset();
    latch_audio_fault();
    uint32_t requests = fake_storage_request_count();

    world_send(PLAYBACK_COMMAND_NEXT);
    assert(world_snapshot().Index == 2U);
    world_send(PLAYBACK_COMMAND_PREV);
    world_send(PLAYBACK_COMMAND_PREV);
    assert(world_snapshot().Index == 0U);

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_AUDIO_FAULT);
    assert(world_snapshot().StopCount == 1U);
    assert(fake_storage_request_count() == requests);

    world_send(PLAYBACK_COMMAND_TOGGLE);   /* 游标移动不解除锁存 */
    assert(world_snapshot().StopCount == 2U);
    assert(world_snapshot().Index == 0U);
    assert(fake_storage_request_count() == requests);
}

/**
 * @brief 切歌途中 Stop 失败：已写的新流请求被“无流”覆盖，停止 AUDIO_FAULT，游标保留在已前进的位置。
 */
static void test_stop_failure_during_track_change_overwrites_the_new_request(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    uint32_t requests = fake_storage_request_count();

    fake_player_fail_next_stop();
    world_send(PLAYBACK_COMMAND_NEXT);

    assert(fake_storage_request_count() == requests + 2U);   /* 新流请求，随后“无流” */
    assert(fake_storage_last_request().StreamSeq == 0U);
    assert(call_log_index_of(CALL_WRITE_REQUEST, requests) < call_log_index_of(CALL_ABANDON, 0U));
    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_AUDIO_FAULT);
    assert(world_snapshot().StopCount == 1U);
    assert(world_snapshot().HasTrack);
    assert(world_snapshot().Index == 1U);
    assert(fake_player_faulted());

    world_send(PLAYBACK_COMMAND_TOGGLE);   /* 此后锁存规则生效 */
    assert(world_snapshot().StopCount == 2U);
    assert(fake_storage_request_count() == requests + 2U);
}

/**
 * @brief 暂停时 Stop 失败：停止 AUDIO_FAULT 并锁存，而不是报暂停。
 */
static void test_stop_failure_on_pause_latches_audio_fault(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);

    fake_player_fail_next_stop();
    world_send(PLAYBACK_COMMAND_TOGGLE);

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_AUDIO_FAULT);
    assert(world_snapshot().StopCount == 1U);
    assert(fake_storage_last_request().StreamSeq == 0U);

    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopCount == 2U);
}

/**
 * @brief 因其他原因停止时 Stop 失败：原因升级为 AUDIO_FAULT 并锁存，停止次数仍只 +1。
 */
static void test_stop_failure_while_stopping_for_another_reason_escalates(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);

    fake_player_fail_next_stop();
    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_READ_ERROR);
    world_wake();

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_AUDIO_FAULT);
    assert(world_snapshot().StopCount == 1U);

    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().StopCount == 2U);
    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
}

/**
 * @brief T_out 到期时 Stop 失败：同样升级为 AUDIO_FAULT。
 */
static void test_stop_failure_on_t_out_expiry_escalates(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    fake_player_emit_output_started();
    world_wake();

    fake_player_fail_next_stop();
    world_wake_at(world_now() + PLAYBACK_MACHINE_OUTPUT_TIMEOUT_MS);

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_AUDIO_FAULT);
    assert(world_snapshot().StopCount == 1U);
}

/**
 * @brief 开启 OK 时播放模块拒绝开新流（STOP_FAILED 已锁存，或上一条流 Stop 失败）：停止 AUDIO_FAULT 并锁存。
 */
static void test_begin_stream_refused_stops_audio_fault(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_NEXT);

    fake_player_refuse_next_begin();
    world_open_ok(GENERATION, TRACKS);

    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_AUDIO_FAULT);
    assert(world_snapshot().StopCount == 1U);
    assert(world_snapshot().Index == 1U);
    assert(fake_storage_last_request().StreamSeq == 0U);

    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().StopCount == 2U);
}

/**
 * @brief 锁存期间已经没有流：任何流记录事实（含拔卡）都不会再改停止原因或重复累加停止次数。
 */
static void test_latched_state_has_no_stream_so_stream_facts_are_ignored(void)
{
    world_reset();
    latch_audio_fault();
    uint32_t old_seq = world_current_seq();

    fake_storage_set_end(old_seq, PLAYBACK_MACHINE_END_CARD_REMOVED);
    world_wake();

    assert(world_snapshot().StopCount == 1U);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_AUDIO_FAULT);
    assert(world_snapshot().HasTrack);
}

/**
 * @brief 运行全部故障测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_transfer_error_stops_audio_error_and_can_be_retried();
    test_pump_failure_stops_audio_error();
    test_stop_failed_stops_audio_fault_and_keeps_cursor();
    test_latched_toggle_only_bumps_stop_count();
    test_latched_select_only_bumps_stop_count();
    test_latched_next_and_prev_only_move_the_cursor();
    test_stop_failure_during_track_change_overwrites_the_new_request();
    test_stop_failure_on_pause_latches_audio_fault();
    test_stop_failure_while_stopping_for_another_reason_escalates();
    test_stop_failure_on_t_out_expiry_escalates();
    test_begin_stream_refused_stops_audio_fault();
    test_latched_state_has_no_stream_so_stream_facts_are_ignored();

    puts("playback_task_faults: all tests passed");
    return 0;
}
