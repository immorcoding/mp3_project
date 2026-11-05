/**
 ******************************************************************************
 * @file    test_playback_task_switch.c
 * @brief   playback_machine 切歌到出声时延（DIAG-6、#204）：Switch INFO 的格式、触发与作废，
 *          以及开机以来的欠载累计。
 * @note    假周期计数器 100 个周期折合 1 µs，所以 4200000 个周期是 42000 µs。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "playback_task_test_support.h"

#define GEN 7U
#define TRACKS 5U
#define CYCLES_PER_US 100U

/**
 * @brief 复位并从停止态开播到首帧解出，且输出已起播（排除开播本身的日志）。
 */
static void start_playing_with_output(void)
{
    world_reset();
    world_start_playing(GEN, TRACKS);
    fake_player_emit_output_started();
    world_wake();
    assert(world_snapshot().State == PLAYBACK_STATE_PLAYING);
}

/**
 * @brief 日志里以 "Switch " 开头的 INFO 条数。
 * @return 条数。
 */
static uint32_t count_switch_logs(void)
{
    uint32_t count = 0U;

    for (uint32_t i = 0U; i < fake_log_count(); i++)
    {
        if (strncmp(fake_log_text(i), "Switch ", 7U) == 0)
        {
            count++;
        }
    }
    return count;
}

/**
 * @brief 最后一条以 "Switch " 开头的 INFO 的文本。
 * @return 文本；没有时断言失败。
 */
static const char *last_switch_log(void)
{
    for (uint32_t i = fake_log_count(); i > 0U; i--)
    {
        if (strncmp(fake_log_text(i - 1U), "Switch ", 7U) == 0)
        {
            return fake_log_text(i - 1U);
        }
    }
    assert(0 && "no Switch log");
    return "";
}

/**
 * @brief 新流跑到输出起播：Storage 应答开启 OK，放入首块，播放模块回报 OutputStarted。
 */
static void complete_switch(void)
{
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);
    fake_player_emit_output_started();
    world_wake();
}

/**
 * @brief 播放中 NEXT：输出重新起播的那一步记一行 Switch，序号是新曲，lat 是命令处理到起播的微秒。
 */
static void test_next_logs_switch_with_latency_when_output_restarts(void)
{
    start_playing_with_output();
    assert(count_switch_logs() == 0U);

    world_send(PLAYBACK_COMMAND_NEXT);
    fake_cycles_advance(42000U * CYCLES_PER_US);
    complete_switch();

    assert(count_switch_logs() == 1U);
    assert(strcmp(last_switch_log(), "Switch idx=1 lat=42000") == 0);
}

/**
 * @brief PREV 与 SELECT 同样记 Switch，序号分别是上一首和被选的那首。
 */
static void test_prev_and_select_log_switch_with_their_index(void)
{
    start_playing_with_output();

    world_send(PLAYBACK_COMMAND_PREV);
    fake_cycles_advance(7000U * CYCLES_PER_US);
    complete_switch();
    assert(strcmp(last_switch_log(), "Switch idx=4 lat=7000") == 0);

    world_send_select(GEN, 2U);
    fake_cycles_advance(9000U * CYCLES_PER_US);
    complete_switch();
    assert(count_switch_logs() == 2U);
    assert(strcmp(last_switch_log(), "Switch idx=2 lat=9000") == 0);
}

/**
 * @brief 输出还没重新起播时不记；起播之后只记一次，之后的推进不重复。
 */
static void test_switch_is_logged_once_and_only_after_output_restarts(void)
{
    start_playing_with_output();

    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);
    assert(count_switch_logs() == 0U);

    fake_player_emit_output_started();
    world_wake();
    world_wake();
    world_wake();
    assert(count_switch_logs() == 1U);
}

/**
 * @brief 开播（TOGGLE）和跳转（SEEK）不是切歌，不记 Switch。
 */
static void test_toggle_start_and_seek_do_not_log_switch(void)
{
    world_reset();
    world_start_playing(GEN, TRACKS);
    fake_player_emit_output_started();
    world_wake();
    assert(count_switch_logs() == 0U);

    world_send_seek(GEN, 0U, 45000U);
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);
    fake_player_emit_output_started();
    world_wake();
    assert(count_switch_logs() == 0U);
}

/**
 * @brief 起播前被停止作废：之后即便输出起播也不补记。
 */
static void test_stop_before_output_restarts_voids_the_switch(void)
{
    start_playing_with_output();

    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_result(PLAYBACK_MACHINE_OPEN_BAD_FILE);
    fake_player_emit_output_started();
    world_wake();

    assert(count_switch_logs() == 0U);
}

/**
 * @brief 起播前又来一次切歌：只记最后一次，lat 从最后一条命令算起。
 */
static void test_latest_switch_wins_and_times_from_the_latest_command(void)
{
    start_playing_with_output();

    world_send(PLAYBACK_COMMAND_NEXT);
    fake_cycles_advance(30000U * CYCLES_PER_US);
    world_send(PLAYBACK_COMMAND_NEXT);
    fake_cycles_advance(5000U * CYCLES_PER_US);
    complete_switch();

    assert(count_switch_logs() == 1U);
    assert(strcmp(last_switch_log(), "Switch idx=2 lat=5000") == 0);
}

/**
 * @brief 暂停中换曲不出声，不记 Switch。
 */
static void test_switch_while_paused_logs_nothing(void)
{
    start_playing_with_output();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().State == PLAYBACK_STATE_PAUSED);

    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_ok(GEN, TRACKS);
    world_feed_block(30U);
    world_wake();

    assert(count_switch_logs() == 0U);
}

/**
 * @brief 欠载累计：每次播放模块回报的次数并入开机以来的总数。
 */
static void test_underrun_total_accumulates_reports(void)
{
    start_playing_with_output();
    assert(playback_machine_underrun_total(world_machine()) == 0U);

    fake_player_emit_underrun();
    world_wake();
    assert(playback_machine_underrun_total(world_machine()) == 1U);

    fake_player_emit_underrun();
    fake_player_emit_underrun();
    world_wake();
    assert(playback_machine_underrun_total(world_machine()) == 3U);

    world_wake();
    assert(playback_machine_underrun_total(world_machine()) == 3U);
}

/**
 * @brief 运行全部用例。
 * @return 0 表示全部通过（失败由 assert 中止）。
 */
int main(void)
{
    test_next_logs_switch_with_latency_when_output_restarts();
    test_prev_and_select_log_switch_with_their_index();
    test_switch_is_logged_once_and_only_after_output_restarts();
    test_toggle_start_and_seek_do_not_log_switch();
    test_stop_before_output_restarts_voids_the_switch();
    test_latest_switch_wins_and_times_from_the_latest_command();
    test_switch_while_paused_logs_nothing();
    test_underrun_total_accumulates_reports();
    puts("playback_task_switch: OK");
    return 0;
}
