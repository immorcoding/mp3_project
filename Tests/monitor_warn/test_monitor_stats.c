/**
 ******************************************************************************
 * @file    test_monitor_stats.c
 * @brief   Monitor 5 s 批次的 CPU 占比与健康行的主机行为测试（DIAG-6、#204）。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "APP/tasks/monitor/monitor_stats.h"

/**
 * @brief 首个窗口（基准全零）从启动算起：占比 = 累计 / 总计数，一位小数向下取整，按键的字典序排列。
 */
static void test_first_window_counts_from_boot(void)
{
    static monitor_cpu_baseline_t baseline;
    const monitor_cpu_sample_t samples[] = {
        {"IDLE", 8630U},
        {"GUI Task", 710U},
        {"Playback Task", 200U},
        {"Storage Task", 40U},
        {"Log Task", 10U},
        {"Monitor Task", 5U},
        {"Tmr Svc", 5U},
    };
    char line[MONITOR_LOG_LINE_LENGTH];

    memset(&baseline, 0, sizeof(baseline));
    monitor_cpu_format(line, sizeof(line), &baseline, samples, 7U, 9600U, 5000U);

    /* 总计数 9600：IDLE 8630 为 89.8 %，GUI 710 为 7.3 %。 */
    assert(strcmp(line, "CPU win=5000ms gui=7.3 idle=89.8 log=0.1 moni=0.0 play=2.0 stor=0.4 tmr=0.0") == 0);
}

/**
 * @brief 之后的窗口只看两次快照的差值，不含更早的累计。
 */
static void test_later_window_uses_deltas(void)
{
    static monitor_cpu_baseline_t baseline;
    const monitor_cpu_sample_t first[] = {{"IDLE", 1000U}, {"GUI Task", 1000U}};
    const monitor_cpu_sample_t second[] = {{"IDLE", 1900U}, {"GUI Task", 1100U}};
    char line[MONITOR_LOG_LINE_LENGTH];

    memset(&baseline, 0, sizeof(baseline));
    monitor_cpu_baseline_store(&baseline, first, 2U, 2000U);
    monitor_cpu_format(line, sizeof(line), &baseline, second, 2U, 3000U, 5000U);

    /* 窗口总计数 1000：IDLE 增 900 为 90.0 %，GUI 增 100 为 10.0 %。 */
    assert(strcmp(line, "CPU win=5000ms gui=10.0 idle=90.0") == 0);
}

/**
 * @brief 计数器 32 位回绕：差值用无符号减法，占比仍正确。
 */
static void test_counter_wrap_still_gives_correct_share(void)
{
    static monitor_cpu_baseline_t baseline;
    const monitor_cpu_sample_t first[] = {{"IDLE", 0xFFFFFF00U}, {"GUI Task", 0xFFFFFF00U}};
    const monitor_cpu_sample_t second[] = {{"IDLE", 0x00000160U}, {"GUI Task", 0x00000020U}};
    char line[MONITOR_LOG_LINE_LENGTH];

    memset(&baseline, 0, sizeof(baseline));
    monitor_cpu_baseline_store(&baseline, first, 2U, 0xFFFFFE00U);
    monitor_cpu_format(line, sizeof(line), &baseline, second, 2U, 0x00000200U, 5000U);

    /* 总增量 0x400 = 1024；IDLE 增 0x260 = 608 为 59.3 %，GUI 增 0x120 = 288 为 28.1 %。 */
    assert(strcmp(line, "CPU win=5000ms gui=28.1 idle=59.3") == 0);
}

/**
 * @brief 基准里没有的新任务按 0 起算；基准里有而本次没有的任务不出现。
 */
static void test_new_and_vanished_tasks(void)
{
    static monitor_cpu_baseline_t baseline;
    const monitor_cpu_sample_t first[] = {{"IDLE", 100U}, {"Gone Task", 100U}};
    const monitor_cpu_sample_t second[] = {{"IDLE", 150U}, {"New Task", 50U}};
    char line[MONITOR_LOG_LINE_LENGTH];

    memset(&baseline, 0, sizeof(baseline));
    monitor_cpu_baseline_store(&baseline, first, 2U, 200U);
    monitor_cpu_format(line, sizeof(line), &baseline, second, 2U, 300U, 5000U);

    assert(strcmp(line, "CPU win=5000ms idle=50.0 new=50.0") == 0);
}

/**
 * @brief 总计数没有前进（零窗口）时不除零，输出“不可用”。
 */
static void test_zero_total_delta_is_unavailable(void)
{
    static monitor_cpu_baseline_t baseline;
    const monitor_cpu_sample_t samples[] = {{"IDLE", 100U}};
    char line[MONITOR_LOG_LINE_LENGTH];

    memset(&baseline, 0, sizeof(baseline));
    monitor_cpu_baseline_store(&baseline, samples, 1U, 100U);
    monitor_cpu_format(line, sizeof(line), &baseline, samples, 1U, 100U, 5000U);

    assert(strcmp(line, "CPU unavailable") == 0);
}

/**
 * @brief 缓冲放不下时末字符改为 '~'，仍以 NUL 结尾且不越界。
 */
static void test_small_buffer_is_truncated_visibly(void)
{
    static monitor_cpu_baseline_t baseline;
    const monitor_cpu_sample_t samples[] = {{"IDLE", 900U}, {"GUI Task", 100U}};
    char line[24];
    size_t length;

    memset(&baseline, 0, sizeof(baseline));
    memset(line, 'x', sizeof(line));
    length = monitor_cpu_format(line, sizeof(line), &baseline, samples, 2U, 1000U, 5000U);

    assert(length == (sizeof(line) - 1U));
    assert(line[sizeof(line) - 2U] == '~');
    assert(line[sizeof(line) - 1U] == '\0');
    assert(strncmp(line, "CPU win=5000ms gui=10.0", 18U) == 0);
}

/**
 * @brief 基准写满后多出的任务不记录、不越界。
 */
static void test_baseline_store_is_bounded(void)
{
    static monitor_cpu_baseline_t baseline;
    monitor_cpu_sample_t samples[MONITOR_CPU_TASK_MAX + 3U];
    static const char *const names[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
                                        "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T"};

    assert((MONITOR_CPU_TASK_MAX + 3U) <= (sizeof(names) / sizeof(names[0])));
    for (unsigned i = 0U; i < (MONITOR_CPU_TASK_MAX + 3U); i++)
    {
        samples[i].name = names[i];
        samples[i].run_time = 10U;
    }
    memset(&baseline, 0, sizeof(baseline));
    monitor_cpu_baseline_store(&baseline, samples, (uint8_t)(MONITOR_CPU_TASK_MAX + 3U), 500U);

    assert(baseline.count == MONITOR_CPU_TASK_MAX);
    assert(baseline.total == 500U);
}

/**
 * @brief 健康行给累计值和本窗口增量；全零时也输出，使“0”本身是证据。
 */
static void test_health_line_shows_totals_and_deltas(void)
{
    const monitor_health_t before = {3U, 0U, 7U};
    const monitor_health_t after = {5U, 2U, 7U};
    const monitor_health_t zero = {0U, 0U, 0U};
    char line[MONITOR_LOG_LINE_LENGTH];

    monitor_health_format(line, sizeof(line), &after, &before);
    assert(strcmp(line, "Health underrun=5(+2) log_svc=2(+2) log_core=7(+0)") == 0);

    monitor_health_format(line, sizeof(line), &zero, &zero);
    assert(strcmp(line, "Health underrun=0(+0) log_svc=0(+0) log_core=0(+0)") == 0);
}

/**
 * @brief 累计值 32 位回绕时增量仍是真实差值。
 */
static void test_health_delta_survives_counter_wrap(void)
{
    const monitor_health_t before = {0xFFFFFFFEU, 0U, 0U};
    const monitor_health_t after = {1U, 0U, 0U};
    char line[MONITOR_LOG_LINE_LENGTH];

    monitor_health_format(line, sizeof(line), &after, &before);
    assert(strcmp(line, "Health underrun=1(+3) log_svc=0(+0) log_core=0(+0)") == 0);
}

/**
 * @brief 最坏取值（三项都是 10 位）仍放进 Service Log 的单条文本上限（127 字符）。
 */
static void test_health_worst_case_fits_service_log_text(void)
{
    const monitor_health_t before = {0U, 0U, 0U};
    const monitor_health_t after = {4294967295U, 4294967295U, 4294967295U};
    char line[MONITOR_LOG_LINE_LENGTH];
    size_t length;

    length = monitor_health_format(line, sizeof(line), &after, &before);
    assert(length < 128U);
    assert(strchr(line, '~') == NULL);
}

/**
 * @brief 运行全部用例。
 * @return 0 表示全部通过（失败由 assert 中止）。
 */
int main(void)
{
    test_first_window_counts_from_boot();
    test_later_window_uses_deltas();
    test_counter_wrap_still_gives_correct_share();
    test_new_and_vanished_tasks();
    test_zero_total_delta_is_unavailable();
    test_small_buffer_is_truncated_visibly();
    test_baseline_store_is_bounded();
    test_health_line_shows_totals_and_deltas();
    test_health_delta_survives_counter_wrap();
    test_health_worst_case_fits_service_log_text();
    puts("monitor_stats: OK");
    return 0;
}
