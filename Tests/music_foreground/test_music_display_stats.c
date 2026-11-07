/**
 ******************************************************************************
 * @file    test_music_display_stats.c
 * @brief   显示延迟汇总的主机测试（DIAG-6、R5.1、R5.2、#218）：三类延迟的样本、拔卡后清空延迟的起点、汇总行的格式与节拍。
 *
 * @details
 *          链接真实的 `music_display_stats.c` 与脚本化的 Service Log 替身（`Tests/stubs/lock_env/`）。累计值是模块静态的，
 *          所以每个用例一个进程（命令行参数选用例）。期望值写成字面量（行格式见 `music_display_stats.h`）。
 *          主机测试只证明累加与格式化的算术，真实的显示延迟只能上板读（R5.1、R5.2）。
 ******************************************************************************
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lock_env_stub.h"

#include "APP/apps/music/music_display_stats.h"
#include "Service/log/log_service.h"

/** @brief 没有任何样本时不投日志，再久也不投。 */
static void test_no_samples_no_line(void)
{
    lock_env_log_reset();
    music_display_stats_poll(10000U);
    music_display_stats_poll(60000U);
    assert(lock_env_log_count() == 0U);
}

/** @brief 三类样本各自记 n、本节拍最大值和开机以来最大值；周期满了投一行，节拍内的 n 与最大值清零，开机以来的最大值保留。 */
static void test_one_line_per_period_with_period_and_overall_maxima(void)
{
    lock_env_log_reset();
    music_display_stats_note_title(80U);
    music_display_stats_note_title(150U);
    music_display_stats_note_window(300U);

    music_display_stats_poll(5000U);
    assert(lock_env_log_count() == 0U); /* 节拍没满。 */

    music_display_stats_poll(10000U);
    assert(lock_env_log_count() == 1U);
    assert(lock_env_log_level(0U) == (uint32_t)SERVICE_LOG_LEVEL_INFO);
    assert(strcmp(lock_env_log_tag(0U), "DISPLAY") == 0);
    assert(strcmp(lock_env_log_text(0U),
                  "title n=2 max=150ms top=150ms win n=1 max=300ms top=300ms clear n=0 max=0ms top=0ms") == 0);

    music_display_stats_poll(30000U);
    assert(lock_env_log_count() == 1U); /* 没有新样本：不投。 */

    music_display_stats_note_title(60U); /* 新节拍：n 与最大值重新算，开机以来的最大值保留 150。 */
    music_display_stats_poll(30000U);
    assert(lock_env_log_count() == 2U);
    assert(strcmp(lock_env_log_text(1U),
                  "title n=1 max=60ms top=150ms win n=0 max=0ms top=300ms clear n=0 max=0ms top=0ms") == 0);
}

/** @brief 拔卡后清空延迟 = GUI 清空 Queue 的时刻减前台记下的卡检测边沿时刻；起点用一次就消费；没有起点不记。 */
static void test_clear_latency_is_measured_from_the_volume_edge_and_the_origin_is_consumed(void)
{
    lock_env_log_reset();
    music_display_stats_note_clear(5000U); /* 没有起点（不是拔卡引起的清空，比如开机的第一份空窗）：不记样本。 */
    music_display_stats_poll(10000U);
    assert(lock_env_log_count() == 0U);

    music_display_stats_set_clear_origin(20000U);
    music_display_stats_note_clear(20095U);
    music_display_stats_note_clear(20500U); /* 起点已消费：第二次清空不算。 */
    music_display_stats_poll(30000U);
    assert(lock_env_log_count() == 1U);
    assert(strcmp(lock_env_log_text(0U),
                  "title n=0 max=0ms top=0ms win n=0 max=0ms top=0ms clear n=1 max=95ms top=95ms") == 0);
}

/** @brief tick 计数回绕：用无符号差算延迟。 */
static void test_a_tick_wrap_does_not_inflate_the_clear_latency(void)
{
    lock_env_log_reset();
    music_display_stats_set_clear_origin(0xFFFFFFF0U);
    music_display_stats_note_clear(0x00000020U);
    music_display_stats_poll(10000U);
    assert(lock_env_log_count() == 1U);
    assert(strstr(lock_env_log_text(0U), "clear n=1 max=48ms top=48ms") != NULL);
}

/** @brief 数字截断：延迟超过 99999 ms 截在 99999，样本数截在 9999，整行不超过 127 个字符。 */
static void test_extreme_values_are_clamped_and_the_line_fits(void)
{
    lock_env_log_reset();
    for (unsigned count = 0U; count < 10050U; count++)
    {
        music_display_stats_note_title(4000000000U);
        music_display_stats_note_window(4000000000U);
    }
    music_display_stats_set_clear_origin(0U);
    music_display_stats_note_clear(4000000000U);
    music_display_stats_poll(10000U);

    assert(lock_env_log_count() == 1U);
    assert(lock_env_log_longest_text() <= 127U);
    assert(strcmp(lock_env_log_text(0U),
                  "title n=9999 max=99999ms top=99999ms win n=9999 max=99999ms top=99999ms clear n=1 max=99999ms top=99999ms") == 0);
}

/**
 * @brief 主机测试入口。
 * @param[in] argc 参数个数。
 * @param[in] argv 参数；argv[1] 为用例名。
 * @return 0 表示通过；用例名未知返回 2。
 */
int main(int argc, char **argv)
{
    const char *name = (argc > 1) ? argv[1] : "";

    if (strcmp(name, "no_samples") == 0)
    {
        test_no_samples_no_line();
    }
    else if (strcmp(name, "period") == 0)
    {
        test_one_line_per_period_with_period_and_overall_maxima();
    }
    else if (strcmp(name, "clear") == 0)
    {
        test_clear_latency_is_measured_from_the_volume_edge_and_the_origin_is_consumed();
    }
    else if (strcmp(name, "wrap") == 0)
    {
        test_a_tick_wrap_does_not_inflate_the_clear_latency();
    }
    else if (strcmp(name, "extreme") == 0)
    {
        test_extreme_values_are_clamped_and_the_line_fits();
    }
    else
    {
        fprintf(stderr, "unknown case: %s\n", name);
        return 2;
    }
    puts("music_display_stats: OK");
    return 0;
}
