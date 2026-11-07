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
 * @brief 同一批里可能同时出现的任务是 12 个（`Create Task` 创建完就自删，早于第一批；永久任务 11 个加开机后几秒内才有的 `Flash Boot Task`）：
 *        每个任务一个互不相同的短键，CPU 行不超过 127 个字符（Service Log 单条容量 128 B），不被截断。极端组合（十个任务各占 10.0 %）
 *        一位小数放不下，整行退化成整数百分比；平常的占比保持一位小数。
 */
static void test_every_registered_task_fits_one_cpu_line(void)
{
    static monitor_cpu_baseline_t baseline;
    static const char *const names[] = {"Audio Read Task", "Log Task",      "Storage Task", "Volume Task", "Flash Boot Task",
                                        "Monitor Task",    "GUI Task",     "Playback Task", "IDLE",        "Tmr Svc",
                                        "Background Task", "Foreground Task"};
    static const char *const keys[] = {"audi=", "log=", "stor=", "volu=", "flas=", "moni=", "gui=", "play=", "idle=", "tmr=", "back=", "fore="};
    enum { TASK_COUNT = 12, IDLE_INDEX = 8 };
    monitor_cpu_sample_t samples[TASK_COUNT];
    char line[MONITOR_LOG_LINE_LENGTH];
    size_t length;

    /* 极端组合：十个任务各占 10.0 %，另两个 0.0 %。一位小数的完整格式是 129 个字符，放不下，退化成整数百分比。 */
    for (size_t index = 0U; index < (size_t)TASK_COUNT; index++)
    {
        samples[index].name = names[index];
        samples[index].run_time = (index >= 10U) ? 0U : 100U;
    }

    memset(&baseline, 0, sizeof(baseline));
    length = monitor_cpu_format(line, sizeof(line), &baseline, samples, (uint8_t)TASK_COUNT, 1000U, 5000U);
    assert(length <= 127U);
    assert(strchr(line, '~') == NULL);
    assert(strchr(line, '.') == NULL);
    assert(strstr(line, "gui=10") != NULL);
    assert(strstr(line, "fore=0") != NULL);
    for (size_t index = 0U; index < (size_t)TASK_COUNT; index++)
    {
        const char *first = strstr(line, keys[index]);

        assert(first != NULL);
        assert(strstr(first + 1, keys[index]) == NULL); /* 每个键只出现一次。 */
    }

    /* 平常的占比：同样 12 个任务，一位小数的完整格式放得下，格式不变。 */
    for (size_t index = 0U; index < (size_t)TASK_COUNT; index++)
    {
        samples[index].run_time = (index == (size_t)IDLE_INDEX) ? 860U : 10U;
    }
    memset(&baseline, 0, sizeof(baseline));
    length = monitor_cpu_format(line, sizeof(line), &baseline, samples, (uint8_t)TASK_COUNT, 1000U, 5000U);
    assert(length <= 127U);
    assert(strchr(line, '~') == NULL);
    assert(strstr(line, "idle=86.0") != NULL);
    assert(strstr(line, "fore=1.0") != NULL);

    for (size_t index = 0U; index < (size_t)TASK_COUNT; index++)
    {
        samples[index].run_time = (index == (size_t)IDLE_INDEX) ? 1000U : 0U;
    }
    memset(&baseline, 0, sizeof(baseline));
    length = monitor_cpu_format(line, sizeof(line), &baseline, samples, (uint8_t)TASK_COUNT, 1000U, 5000U);
    assert(length <= 127U);
    assert(strchr(line, '~') == NULL);
    assert(strstr(line, "idle=100.0") != NULL);
}

/**
 * @brief Create Task 的短键 `crea` 与其余 12 个已登记任务互不相同：13 个名字放进同一批（占比都为 0，行宽不是本用例要测的），
 *        每个键恰好出现一次。
 */
static void test_create_task_key_is_distinct_from_the_other_registered_tasks(void)
{
    static monitor_cpu_baseline_t baseline;
    static const char *const names[] = {"Create Task", "Audio Read Task", "Log Task",      "Storage Task", "Volume Task", "Flash Boot Task",
                                        "Monitor Task", "GUI Task",       "Playback Task", "IDLE",         "Tmr Svc",     "Background Task",
                                        "Foreground Task"};
    static const char *const keys[] = {"crea=", "audi=", "log=", "stor=", "volu=", "flas=", "moni=", "gui=", "play=", "idle=", "tmr=", "back=", "fore="};
    enum { TASK_COUNT = 13 };
    monitor_cpu_sample_t samples[TASK_COUNT];
    char line[MONITOR_LOG_LINE_LENGTH];
    size_t length;

    for (size_t index = 0U; index < (size_t)TASK_COUNT; index++)
    {
        samples[index].name = names[index];
        samples[index].run_time = 0U;
    }
    memset(&baseline, 0, sizeof(baseline));
    length = monitor_cpu_format(line, sizeof(line), &baseline, samples, (uint8_t)TASK_COUNT, 1000U, 5000U);
    assert(length <= 127U);
    assert(strchr(line, '~') == NULL);
    for (size_t index = 0U; index < (size_t)TASK_COUNT; index++)
    {
        const char *first = strstr(line, keys[index]);

        assert(first != NULL);
        assert(strstr(first + 1, keys[index]) == NULL);
    }
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
    test_every_registered_task_fits_one_cpu_line();
    test_create_task_key_is_distinct_from_the_other_registered_tasks();
    puts("monitor_stats: OK");
    return 0;
}
