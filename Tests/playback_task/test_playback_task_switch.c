/**
 ******************************************************************************
 * @file    test_playback_task_switch.c
 * @brief   playback_machine 切歌到出声时延与欠载累计的主机行为测试（spike #191）。
 * @note    时延从用户换曲命令（NEXT、PREV、SELECT）被状态机处理算起，到输出重新起播（OutputStarted）；
 *          假周期计数器 100 个周期折合 1 µs。上报走 ReportSwitch，不进 INFO 日志。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "playback_task_test_support.h"

#define GEN 7U
#define TRACKS 5U

/**
 * @brief 复位并从停止态开播到首帧解出（曲目 {GEN, 0}）。
 */
static void start_playing(void)
{
    world_reset();
    world_start_playing(GEN, TRACKS);
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
    assert(fake_switch_report_count() == 0U);
}

/**
 * @brief 把换曲后的新流跑到输出起播：Storage 应答开启 OK，放入首块，播放模块回报 OutputStarted。
 */
static void complete_switch(void)
{
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);
    fake_player_emit_output_started();
    world_wake();
}

/**
 * @brief NEXT 之后输出起播：报告新曲位置和命令处理到起播的时延，且只报一次。
 */
static void test_next_reports_latency_once(void)
{
    start_playing();

    world_send(PLAYBACK_COMMAND_NEXT);
    fake_cycles_advance(25000U);
    complete_switch();

    assert(fake_switch_report_count() == 1U);
    assert(fake_switch_last_index() == 1U);
    assert(fake_switch_last_latency_us() == 250U);

    /* 同一首后续的起播事件（例如欠载恢复后的重新 Start）不再报告。 */
    fake_player_emit_output_started();
    world_wake();
    assert(fake_switch_report_count() == 1U);
}

/**
 * @brief PREV 与 SELECT 同样计时；SELECT 报告所选曲目位置。
 */
static void test_prev_and_select_are_measured(void)
{
    start_playing();

    world_send_select(GEN, 3U);
    fake_cycles_advance(10000U);
    complete_switch();
    assert(fake_switch_report_count() == 1U);
    assert(fake_switch_last_index() == 3U);
    assert(fake_switch_last_latency_us() == 100U);

    world_send(PLAYBACK_COMMAND_PREV);
    fake_cycles_advance(40000U);
    complete_switch();
    assert(fake_switch_report_count() == 2U);
    assert(fake_switch_last_index() == 2U);
    assert(fake_switch_last_latency_us() == 400U);
}

/**
 * @brief 连按：后一条换曲命令重新计时，只报最后一首。
 */
static void test_rapid_second_switch_restarts_timing(void)
{
    start_playing();

    world_send(PLAYBACK_COMMAND_NEXT);
    fake_cycles_advance(50000U);
    world_send(PLAYBACK_COMMAND_NEXT);
    fake_cycles_advance(20000U);
    complete_switch();

    assert(fake_switch_report_count() == 1U);
    assert(fake_switch_last_index() == 2U);
    assert(fake_switch_last_latency_us() == 200U);
}

/**
 * @brief 跳转不是换曲：输出重新起播不报告切歌时延。
 */
static void test_seek_is_not_a_switch(void)
{
    start_playing();

    world_send_seek(GEN, 0U, 90000U);
    fake_cycles_advance(10000U);
    complete_switch();

    assert(fake_switch_report_count() == 0U);
}

/**
 * @brief 暂停中换曲不出声，所以不计时；停止使还没报告的计时作废。
 */
static void test_paused_switch_and_stop_do_not_report(void)
{
    start_playing();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);
    world_send(PLAYBACK_COMMAND_NEXT);
    fake_cycles_advance(10000U);
    complete_switch();
    assert(fake_switch_report_count() == 0U);

    start_playing();
    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_result(PLAYBACK_MACHINE_OPEN_NO_TRACK);
    assert(world_snapshot().State == PLAYBACK_STATE_STOPPED);
    fake_cycles_advance(10000U);
    fake_player_emit_output_started();
    world_wake();
    assert(fake_switch_report_count() == 0U);
}

/**
 * @brief 欠载次数在上层累加：每次推进回报的欠载数相加，不随下一次 Start 清零。
 */
static void test_underruns_accumulate(void)
{
    start_playing();
    assert(playback_machine_underrun_total(world_machine()) == 0U);

    fake_player_emit_underrun();
    world_wake();
    assert(playback_machine_underrun_total(world_machine()) == 1U);

    fake_player_emit_underrun();
    fake_player_emit_underrun();
    world_wake();
    assert(playback_machine_underrun_total(world_machine()) >= 2U);
}

/**
 * @brief 运行全部用例。
 * @return 全部断言通过时返回 0。
 */
int main(void)
{
    test_next_reports_latency_once();
    test_prev_and_select_are_measured();
    test_rapid_second_switch_restarts_timing();
    test_seek_is_not_a_switch();
    test_paused_switch_and_stop_do_not_report();
    test_underruns_accumulate();
    printf("playback_task_switch tests passed\n");
    return 0;
}
