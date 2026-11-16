/**
 ******************************************************************************
 * @file    test_ui_cell_stats.c
 * @brief   ui_cell 持锁统计的主机测试（DIAG-6、#218）：持锁最长、GUI 一侧不等入口的尝试与拿不到的次数、汇总行的格式与节拍。
 *
 * @details
 *          链接真实的 `ui_cell_stats.c`、脚本化的周期计数器与 Service Log 替身（`Tests/stubs/lock_env/`）。
 *          期望值写成字面量（行格式见 `APP/ui_cell/README.md`），不用代码里的表达式重算。
 *          主机测试是单线程的：这里只证明累加与格式化的算术，真实持锁时长和 GUI 拿不到锁的比例只能上板读（R5.3）。
 ******************************************************************************
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lock_env_stub.h"

#include "APP/ui_cell/ui_cell.h"
#include "APP/ui_cell/ui_cell_stats.h"
#include "Service/log/log_service.h"

/**
 * @brief 开始一个用例：假周期计数器与日志复位。累计值是模块静态的，所以每个用例一个进程（命令行参数选用例）。
 */
static void begin_case(void)
{
    lock_env_counter_reset();
    lock_env_log_reset();
}

/** @brief 持锁最长值是所有持有里最长的一次（微秒），不是总和，也不会因为后来更短的持有而下降。 */
static void test_hold_max_is_the_longest_single_hold(void)
{
    UiCell_StatsTypeDef stats;
    uint32_t start;

    begin_case();

    start = ui_cell_stats_hold_begin();
    lock_env_advance_us(7U);
    ui_cell_stats_hold_end(start);

    start = ui_cell_stats_hold_begin();
    lock_env_advance_us(120U);
    ui_cell_stats_hold_end(start);

    start = ui_cell_stats_hold_begin();
    lock_env_advance_us(30U);
    ui_cell_stats_hold_end(start);

    ui_cell_stats_get(&stats);
    assert(stats.HoldMaxUs == 120U);
    assert(stats.Acquisitions == 3U);
}

/** @brief 持锁时间用无符号周期差：32 位周期计数器回绕不影响结果。 */
static void test_hold_survives_a_cycle_counter_wrap(void)
{
    UiCell_StatsTypeDef stats;
    uint32_t start;

    begin_case();
    lock_env_advance_us(8947000U); /* 480 MHz 下约 4.29e9 周期，贴近 32 位上限。 */
    start = ui_cell_stats_hold_begin();
    lock_env_advance_us(2000U);
    ui_cell_stats_hold_end(start);

    ui_cell_stats_get(&stats);
    assert(stats.HoldMaxUs == 2000U);
}

/** @brief GUI 一侧的不等入口：每次尝试计一次，拿不到的另计一次 busy。 */
static void test_gui_tries_and_busy_are_counted_separately(void)
{
    UiCell_StatsTypeDef stats;

    begin_case();
    ui_cell_stats_gui_try(false);
    ui_cell_stats_gui_try(false);
    ui_cell_stats_gui_try(true);

    ui_cell_stats_get(&stats);
    assert(stats.GuiTries == 3U);
    assert(stats.GuiBusy == 1U);
}

/** @brief 没有数据时不投日志；有数据但周期没满也不投；周期满了投一行，之后没有新数据就不再投。 */
static void test_poll_posts_one_line_per_period_only_when_there_is_new_data(void)
{
    uint32_t start;

    begin_case();
    ui_cell_stats_poll(10000U);
    assert(lock_env_log_count() == 0U); /* 没有任何数据。 */

    start = ui_cell_stats_hold_begin();
    lock_env_advance_us(9U);
    ui_cell_stats_hold_end(start);
    ui_cell_stats_gui_try(false);
    ui_cell_stats_gui_try(false);
    ui_cell_stats_gui_try(true);

    ui_cell_stats_poll(5000U);
    assert(lock_env_log_count() == 0U); /* 周期没满。 */

    ui_cell_stats_poll(10000U);
    assert(lock_env_log_count() == 1U);
    assert(lock_env_log_level(0U) == (uint32_t)SERVICE_LOG_LEVEL_INFO);
    assert(strcmp(lock_env_log_tag(0U), "UICELL") == 0);
    assert(strcmp(lock_env_log_text(0U), "n=1 hold=9us gui=3 busy=1 pct=33.3") == 0);

    ui_cell_stats_poll(30000U);
    assert(lock_env_log_count() == 1U); /* 没有新数据：不再投，多久都不投。 */

    ui_cell_stats_gui_try(false);
    ui_cell_stats_poll(30000U);         /* 有新数据，距上一行（10000 ms）已满周期：投。 */
    assert(lock_env_log_count() == 2U);
    assert(strcmp(lock_env_log_text(1U), "n=1 hold=9us gui=4 busy=1 pct=25.0") == 0); /* 累计值，最大值不清零。 */

    ui_cell_stats_gui_try(true);
    ui_cell_stats_poll(33000U);         /* 又有新数据，但距上一行才 3 s：先不投。 */
    assert(lock_env_log_count() == 2U);
    ui_cell_stats_poll(40000U);
    assert(lock_env_log_count() == 3U);
    assert(strcmp(lock_env_log_text(2U), "n=1 hold=9us gui=5 busy=2 pct=40.0") == 0);
}

/** @brief GUI 没尝试过时占比是 0.0，不会除零。 */
static void test_percentage_is_zero_without_gui_attempts(void)
{
    uint32_t start;

    begin_case();
    start = ui_cell_stats_hold_begin();
    ui_cell_stats_hold_end(start);
    ui_cell_stats_poll(10000U);

    assert(lock_env_log_count() == 1U);
    assert(strcmp(lock_env_log_text(0U), "n=1 hold=0us gui=0 busy=0 pct=0.0") == 0);
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

    if (strcmp(name, "hold_max") == 0)
    {
        test_hold_max_is_the_longest_single_hold();
    }
    else if (strcmp(name, "wrap") == 0)
    {
        test_hold_survives_a_cycle_counter_wrap();
    }
    else if (strcmp(name, "gui_tries") == 0)
    {
        test_gui_tries_and_busy_are_counted_separately();
    }
    else if (strcmp(name, "poll_period") == 0)
    {
        test_poll_posts_one_line_per_period_only_when_there_is_new_data();
    }
    else if (strcmp(name, "zero_gui") == 0)
    {
        test_percentage_is_zero_without_gui_attempts();
    }
    else
    {
        fprintf(stderr, "unknown case: %s\n", name);
        return 2;
    }
    puts("ui_cell_stats: OK");
    return 0;
}
