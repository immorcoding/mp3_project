/**
 ******************************************************************************
 * @file    test_music_hook_pageturn.c
 * @brief   翻页注入测试钩子的主机测试（DIAG-7、R5.4、#218）：默认关闭时什么都不做；打开时启动行、块池补满耗时、超时。
 *
 * @details
 *          链接真实的 `music_hook_pageturn.c` 与脚本化的 Service Log 替身；块池就绪数和空闲数在本文件里用假实现替换。
 *          同一份测试源编两个配置：默认（钩子关）和 `MUSIC_HOOK_PAGE_TURN=1`（钩子开）。注入时刻与测量状态是模块静态的，
 *          所以每个用例一个进程。期望值写成字面量；块池补满的真实耗时只能上板读。
 ******************************************************************************
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lock_env_stub.h"

#include "APP/apps/music/music_hook_pageturn.h"
#include "Service/log/log_service.h"

static uint32_t g_ready_blocks;
static uint32_t g_free_blocks;

/** @brief 假块池就绪数。 @return 脚本给出的就绪块数。 */
uint32_t audio_reader_block_ready_count(void)
{
    return g_ready_blocks;
}

/** @brief 假块池空闲数。 @return 脚本给出的空闲块数。 */
uint32_t audio_reader_block_free_count(void)
{
    return g_free_blocks;
}

/** @brief 钩子关闭（默认）：启动行不投、注入不记、测量不进行，前台任务什么都不用做。 */
static void test_default_build_does_nothing(void)
{
    lock_env_log_reset();
    g_ready_blocks = 0U;

    music_hook_pageturn_announce();
    music_hook_pageturn_note_injection(1000U);
    assert(music_hook_pageturn_poll(1100U) == UINT32_MAX);
    g_ready_blocks = 8U;
    assert(music_hook_pageturn_poll(1200U) == UINT32_MAX);
    assert(lock_env_log_count() == 0U);
}

/** @brief 钩子打开：启动行是一条 WARN `page_turn ON ...`，标签 HOOK（串口里 `HOOK: page_turn ON`）。 */
static void test_hook_announces_itself(void)
{
    lock_env_log_reset();
    music_hook_pageturn_announce();

    assert(lock_env_log_count() == 1U);
    assert(lock_env_log_level(0U) == (uint32_t)SERVICE_LOG_LEVEL_WARN);
    assert(strcmp(lock_env_log_tag(0U), "HOOK") == 0);
    assert(strcmp(lock_env_log_text(0U), "page_turn ON watermark=4 timeout=5000ms") == 0);
}

/** @brief 没有注入时前台任务不用回头量块池（等通知）。 */
static void test_no_injection_means_no_measurement(void)
{
    lock_env_log_reset();
    g_ready_blocks = 9U;
    assert(music_hook_pageturn_poll(5000U) == UINT32_MAX);
    assert(lock_env_log_count() == 0U);
}

/** @brief 注入之后每 1 ms 回头量一次；就绪块数回到安全水位（4 块）的那一刻记一行补满耗时，之后停止测量。 */
static void test_refill_is_measured_from_the_injection_to_the_watermark(void)
{
    lock_env_log_reset();
    g_ready_blocks = 1U;
    g_free_blocks = 20U;
    music_hook_pageturn_note_injection(1000U);

    assert(music_hook_pageturn_poll(1000U) == 1U);
    g_ready_blocks = 3U;
    g_free_blocks = 18U;
    assert(music_hook_pageturn_poll(1090U) == 1U);
    assert(lock_env_log_count() == 0U);

    g_ready_blocks = 4U;
    g_free_blocks = 17U;
    assert(music_hook_pageturn_poll(1141U) == UINT32_MAX);
    assert(lock_env_log_count() == 1U);
    assert(lock_env_log_level(0U) == (uint32_t)SERVICE_LOG_LEVEL_INFO);
    assert(strcmp(lock_env_log_tag(0U), "HOOK") == 0);
    assert(strcmp(lock_env_log_text(0U), "page_turn refilled after=141ms ready=4 free=17") == 0);

    assert(music_hook_pageturn_poll(1200U) == UINT32_MAX); /* 一次注入只记一次。 */
    assert(lock_env_log_count() == 1U);
}

/** @brief 一直补不满：超过上界（5 s）记一行 WARN 放弃，不无限等。 */
static void test_a_refill_that_never_completes_times_out(void)
{
    lock_env_log_reset();
    g_ready_blocks = 0U;
    g_free_blocks = 31U;
    music_hook_pageturn_note_injection(2000U);

    assert(music_hook_pageturn_poll(6999U) == 1U);
    assert(lock_env_log_count() == 0U);
    assert(music_hook_pageturn_poll(7000U) == UINT32_MAX);
    assert(lock_env_log_count() == 1U);
    assert(lock_env_log_level(0U) == (uint32_t)SERVICE_LOG_LEVEL_WARN);
    assert(strcmp(lock_env_log_text(0U), "page_turn timeout after=5000ms ready=0 free=31") == 0);
    assert(music_hook_pageturn_poll(7100U) == UINT32_MAX);
}

/** @brief 上一次测量没完成时又注入了一次：以新的注入时刻为准。 */
static void test_a_new_injection_replaces_an_unfinished_measurement(void)
{
    lock_env_log_reset();
    g_ready_blocks = 0U;
    music_hook_pageturn_note_injection(1000U);
    assert(music_hook_pageturn_poll(1050U) == 1U);

    music_hook_pageturn_note_injection(1100U);
    g_ready_blocks = 4U;
    g_free_blocks = 10U;
    assert(music_hook_pageturn_poll(1200U) == UINT32_MAX);
    assert(strcmp(lock_env_log_text(0U), "page_turn refilled after=100ms ready=4 free=10") == 0);
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

    if (strcmp(name, "default_off") == 0)
    {
        test_default_build_does_nothing();
    }
    else if (strcmp(name, "announce") == 0)
    {
        test_hook_announces_itself();
    }
    else if (strcmp(name, "no_injection") == 0)
    {
        test_no_injection_means_no_measurement();
    }
    else if (strcmp(name, "refill") == 0)
    {
        test_refill_is_measured_from_the_injection_to_the_watermark();
    }
    else if (strcmp(name, "timeout") == 0)
    {
        test_a_refill_that_never_completes_times_out();
    }
    else if (strcmp(name, "replace") == 0)
    {
        test_a_new_injection_replaces_an_unfinished_measurement();
    }
    else
    {
        fprintf(stderr, "unknown case: %s\n", name);
        return 2;
    }
    puts("music_hook_pageturn: OK");
    return 0;
}
