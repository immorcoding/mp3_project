/**
 ******************************************************************************
 * @file    test_background_reclaim.c
 * @brief   Flash 回收作业的主机测试（#220，STOR-12）：周期与一次最多一块、卷序号为 0 与卷未挂载时不调用、
 *          失败一次就停并记一条 ERROR、卷序号变化后恢复。
 *
 * @details
 *          链接真实的 `background_reclaim.c`、`volume_event.c`、共用 FreeRTOS 桩与 Service Log 替身；
 *          Filesystem Service 的 `Service_Filesystem_ReclaimFlash` 在本文件里用脚本化的假实现替换。
 *          回收作业和卷事件的状态是静态的，每个用例一个进程（命令行参数选用例）。
 *          期望值来自 #202 F 节与 STOR-12 现行文字（Flash 就绪后开始、100 ms 周期、一次最多一块、失败一次就停、
 *          卷序号为 0 直接返回），不用代码里的表达式重算。
 *          主机测试是单线程的：证明每一步调不调回收、返回多久后再叫、失败后停在哪、日志内容；
 *          不证明真实的擦除时长、与音频读取的并发是否互不影响、播放中真擦除欠载为 0（靠第 6 次上板 R6.2）。
 ******************************************************************************
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos_stub.h"
#include "lock_env_stub.h"

#include "APP/tasks/background/background_reclaim.h"
#include "APP/tasks/background/background_task_config.h"
#include "APP/tasks/volume/volume_event.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/log/log_service.h"

/* 脚本化的假 ReclaimFlash：每次调用取下一个状态，用完返回 OK；记录调用次数。 */
#define SCRIPT_MAX 64U
static Service_StatusTypeDef g_script[SCRIPT_MAX];
static uint32_t g_script_length;
static uint32_t g_reclaim_calls;
static TickType_t g_reclaim_tick[256];

/**
 * @brief  假 ReclaimFlash：记录调用时刻，按脚本返回状态。
 * @return 脚本里的下一个状态，脚本用完为 SERVICE_OK。
 */
Service_StatusTypeDef Service_Filesystem_ReclaimFlash(void)
{
    const uint32_t index = g_reclaim_calls;

    if (index < (sizeof(g_reclaim_tick) / sizeof(g_reclaim_tick[0])))
    {
        g_reclaim_tick[index] = xTaskGetTickCount();
    }
    g_reclaim_calls++;
    return (index < g_script_length) ? g_script[index] : SERVICE_OK;
}

/**
 * @brief 开始一个场景：脚本与计数清空，桩复位，日志清空，tick 设为给定值。
 * @param[in] tick 起始 tick。
 */
static void begin_scenario(TickType_t tick)
{
    memset(g_script, 0, sizeof(g_script));
    g_script_length = 0U;
    g_reclaim_calls = 0U;
    freertos_stub_reset();
    freertos_stub_set_tick(tick);
    lock_env_log_reset();
}

/**
 * @brief 把假时间推进 ms 毫秒（1 kHz，一毫秒一个 tick）。
 * @param[in] ms 毫秒数。
 */
static void advance_ms(uint32_t ms)
{
    freertos_stub_set_tick(xTaskGetTickCount() + (TickType_t)ms);
}

/**
 * @brief Flash 卷就绪：发布卷事件，后台任务被叫醒，作业第一次看到新序号（记下起点，返回一个周期）。
 * @note  真实任务里叫醒作业的就是这个卷事件，所以“就绪”与“第一次看到”在同一时刻。
 */
static void flash_becomes_ready(void)
{
    (void)volume_publish_ready(VOLUME_ID_FLASH);
    assert(background_reclaim_step() == 100U);
    assert(g_reclaim_calls == 0U);
}

/** @brief 开机至今 Flash 卷序号为 0（没发过卷事件）：直接返回，不碰 Flash 卷，也不记日志，时间过去多久都一样。 */
static void test_sequence_zero_does_nothing(void)
{
    begin_scenario(1000U);
    assert(volume_status(VOLUME_ID_FLASH).Sequence == 0U);

    for (uint32_t round = 0U; round < 20U; round++)
    {
        assert(background_reclaim_step() == BACKGROUND_WAIT_FOREVER);
        advance_ms(5000U);
    }
    assert(g_reclaim_calls == 0U);
    assert(lock_env_log_count() == 0U);
}

/** @brief Flash 卷就绪之后第一次回收机会在一个周期（100 ms）之后：之前只回报还剩多久，不调回收。 */
static void test_first_reclaim_comes_one_period_after_ready(void)
{
    begin_scenario(1000U);
    (void)volume_publish_ready(VOLUME_ID_FLASH);

    assert(background_reclaim_step() == 100U);
    assert(g_reclaim_calls == 0U);
    advance_ms(99U);
    assert(background_reclaim_step() == 1U);
    assert(g_reclaim_calls == 0U);
    advance_ms(1U);
    assert(background_reclaim_step() == 100U); /* 到点：调一次，下一次又是 100 ms 之后 */
    assert(g_reclaim_calls == 1U);
}

/** @brief 一次最多一块：落后很多个周期也只调一次回收，不补回错过的机会。 */
static void test_one_reclaim_per_step_even_when_many_periods_late(void)
{
    begin_scenario(1000U);
    (void)volume_publish_ready(VOLUME_ID_FLASH);
    assert(background_reclaim_step() == 100U);

    advance_ms(10000U);
    assert(background_reclaim_step() == 100U);
    assert(g_reclaim_calls == 1U);
    assert(background_reclaim_step() == 100U); /* 同一时刻再叫它：还没到下一个周期 */
    assert(g_reclaim_calls == 1U);
}

/** @brief 被别的作业或通知频繁叫醒时周期不变：每 10 ms 调一次作业，1 s 内恰好 10 次回收，间隔 100 ms。 */
static void test_period_holds_when_the_task_is_woken_often(void)
{
    begin_scenario(1000U);
    (void)volume_publish_ready(VOLUME_ID_FLASH);

    for (uint32_t lap = 0U; lap < 100U; lap++)
    {
        (void)background_reclaim_step();
        advance_ms(10U);
    }
    (void)background_reclaim_step();

    assert(g_reclaim_calls == 10U);
    for (uint32_t index = 1U; index < 10U; index++)
    {
        assert((TickType_t)(g_reclaim_tick[index] - g_reclaim_tick[index - 1U]) == 100U);
    }
}

/**
 * @brief 断言日志里恰有一条 ERROR，标签 FLASH，正文为 expected，且不超过 Service Log 单条容量。
 * @param[in] expected 期望的正文。
 */
static void assert_single_error_line(const char *expected)
{
    assert(lock_env_log_count() == 1U);
    assert(strcmp(lock_env_log_tag(0U), "FLASH") == 0);
    assert(lock_env_log_level(0U) == (uint32_t)SERVICE_LOG_LEVEL_ERROR);
    assert(strcmp(lock_env_log_text(0U), expected) == 0);
    assert(lock_env_log_longest_text() <= 127U);
}

/** @brief 失败一次就停：记一条 ERROR（带状态数值），之后无论过多久都不再调回收、不再记日志，作业只等卷事件。 */
static void test_failure_stops_with_one_error_line(void)
{
    begin_scenario(1000U);
    g_script[0] = SERVICE_ERROR;
    g_script_length = 1U;
    flash_becomes_ready();

    advance_ms(100U);
    assert(background_reclaim_step() == BACKGROUND_WAIT_FOREVER);
    assert(g_reclaim_calls == 1U);
    assert_single_error_line("Reclaim failed status=1, stopped until Flash volume sequence changes");

    for (uint32_t round = 0U; round < 50U; round++)
    {
        advance_ms(1000U);
        assert(background_reclaim_step() == BACKGROUND_WAIT_FOREVER);
    }
    assert(g_reclaim_calls == 1U);
    assert(lock_env_log_count() == 1U);
}

/** @brief 超时同样算失败：第二次调用返回 TIMEOUT，停在那里，日志里的状态数值是 5。 */
static void test_timeout_failure_stops(void)
{
    begin_scenario(1000U);
    g_script[1] = SERVICE_TIMEOUT;
    g_script_length = 2U;
    flash_becomes_ready();

    advance_ms(100U);
    assert(background_reclaim_step() == 100U);
    advance_ms(100U);
    assert(background_reclaim_step() == BACKGROUND_WAIT_FOREVER);
    assert(g_reclaim_calls == 2U);
    assert_single_error_line("Reclaim failed status=5, stopped until Flash volume sequence changes");

    advance_ms(5000U);
    assert(background_reclaim_step() == BACKGROUND_WAIT_FOREVER);
    assert(g_reclaim_calls == 2U);
}

/** @brief 卷不可用类和忙碌的返回值不是失败：不停、不记日志，下个周期照常给机会。 */
static void test_volume_unavailable_and_busy_results_are_not_failures(void)
{
    begin_scenario(1000U);
    g_script[0] = SERVICE_NO_MEDIA;
    g_script[1] = SERVICE_NOT_READY;
    g_script[2] = SERVICE_INVALID_HANDLE;
    g_script[3] = SERVICE_BUSY;
    g_script_length = 4U;
    flash_becomes_ready();

    for (uint32_t round = 0U; round < 5U; round++)
    {
        advance_ms(100U);
        assert(background_reclaim_step() == 100U);
    }
    assert(g_reclaim_calls == 5U);
    assert(lock_env_log_count() == 0U);
}

/** @brief 失败停下之后，Flash 卷序号变化（卸载再挂载）就恢复：从就绪时刻起一个周期后重新给机会，不再记旧的失败。 */
static void test_sequence_change_resumes_after_a_stop(void)
{
    begin_scenario(1000U);
    g_script[0] = SERVICE_ERROR;
    g_script_length = 1U;
    flash_becomes_ready();
    advance_ms(100U);
    assert(background_reclaim_step() == BACKGROUND_WAIT_FOREVER);
    assert(g_reclaim_calls == 1U);

    (void)volume_publish_gone(VOLUME_ID_FLASH);
    (void)volume_publish_ready(VOLUME_ID_FLASH); /* 序号跳 2：旧的全部作废，再看现状 */
    assert(background_reclaim_step() == 100U);
    assert(g_reclaim_calls == 1U);
    advance_ms(100U);
    assert(background_reclaim_step() == 100U);
    assert(g_reclaim_calls == 2U);
    assert(lock_env_log_count() == 1U);
}

/** @brief 卷没挂载（卷消失事件之后）时不调回收，只等卷事件；再挂载后一个周期才给机会。 */
static void test_unmounted_volume_is_not_called(void)
{
    begin_scenario(1000U);
    (void)volume_publish_ready(VOLUME_ID_FLASH);
    (void)volume_publish_gone(VOLUME_ID_FLASH);

    for (uint32_t round = 0U; round < 10U; round++)
    {
        advance_ms(1000U);
        assert(background_reclaim_step() == BACKGROUND_WAIT_FOREVER);
    }
    assert(g_reclaim_calls == 0U);

    (void)volume_publish_ready(VOLUME_ID_FLASH);
    assert(background_reclaim_step() == 100U);
    assert(g_reclaim_calls == 0U);
    advance_ms(100U);
    assert(background_reclaim_step() == 100U);
    assert(g_reclaim_calls == 1U);
}

typedef struct
{
    const char *name;
    void (*run)(void);
} Case;

static const Case cases[] = {
    {"sequence_zero_does_nothing", test_sequence_zero_does_nothing},
    {"first_reclaim_after_one_period", test_first_reclaim_comes_one_period_after_ready},
    {"one_reclaim_per_step", test_one_reclaim_per_step_even_when_many_periods_late},
    {"period_holds_when_woken_often", test_period_holds_when_the_task_is_woken_often},
    {"failure_stops_with_one_error", test_failure_stops_with_one_error_line},
    {"timeout_failure_stops", test_timeout_failure_stops},
    {"unavailable_and_busy_are_not_failures", test_volume_unavailable_and_busy_results_are_not_failures},
    {"sequence_change_resumes", test_sequence_change_resumes_after_a_stop},
    {"unmounted_is_not_called", test_unmounted_volume_is_not_called},
};

/**
 * @brief 运行命令行指定的用例（模块状态是静态的，每个用例一个进程）。
 * @param[in] argc 参数个数。
 * @param[in] argv 参数。
 * @return 0 表示通过（失败由 assert 中止）。
 */
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        (void)fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }

    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++)
    {
        if (strcmp(cases[index].name, argv[1]) == 0)
        {
            cases[index].run();
            (void)printf("background_reclaim %s: OK\n", argv[1]);
            return 0;
        }
    }

    (void)fprintf(stderr, "unknown case: %s\n", argv[1]);
    return 2;
}
