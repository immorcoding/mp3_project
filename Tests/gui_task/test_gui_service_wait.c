/**
 ******************************************************************************
 * @file    test_gui_service_wait.c
 * @brief   GUI 每圈睡眠时长计算的主机行为测试（#190）。
 ******************************************************************************
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "Service/gui/gui_service_wait.h"

/** @brief 与 lv_conf.h 的 LV_DISP_DEF_REFR_PERIOD 同值的测试上限。 */
#define TEST_REFRESH_PERIOD_MS 16U

/**
 * @brief 睡眠时长要扣掉本圈已经用掉的时间，不能直接按 handler 返回值睡。
 */
static void test_subtracts_round_time_from_handler_value(void)
{
    assert(service_gui_wait_ms(16U, 5U, false, TEST_REFRESH_PERIOD_MS) == 11U);
    assert(service_gui_wait_ms(16U, 15U, false, TEST_REFRESH_PERIOD_MS) == 1U);
    assert(service_gui_wait_ms(10U, 3U, false, TEST_REFRESH_PERIOD_MS) == 7U);
}

/**
 * @brief 本圈耗时不小于 handler 返回值时只睡下限，不出现下溢。
 */
static void test_round_longer_than_handler_value_sleeps_minimum(void)
{
    assert(service_gui_wait_ms(16U, 16U, false, TEST_REFRESH_PERIOD_MS) == 1U);
    assert(service_gui_wait_ms(16U, 40U, false, TEST_REFRESH_PERIOD_MS) == 1U);
    assert(service_gui_wait_ms(0U, 0U, false, TEST_REFRESH_PERIOD_MS) == 1U);
    assert(service_gui_wait_ms(0U, UINT32_MAX, false, TEST_REFRESH_PERIOD_MS) == 1U);
}

/**
 * @brief 上限是保险：handler 返回值大于刷新周期，或没有定时器待办时夹到上限。
 */
static void test_clamps_to_refresh_period(void)
{
    assert(service_gui_wait_ms(30U, 0U, false, TEST_REFRESH_PERIOD_MS) == TEST_REFRESH_PERIOD_MS);
    assert(service_gui_wait_ms(UINT32_MAX, 0U, false, TEST_REFRESH_PERIOD_MS) == TEST_REFRESH_PERIOD_MS);
    assert(service_gui_wait_ms(UINT32_MAX, 3U, false, TEST_REFRESH_PERIOD_MS) == TEST_REFRESH_PERIOD_MS);
}

/**
 * @brief 输入槽里有待取走的命令时只睡下限，与 handler 返回值和本圈耗时无关。
 */
static void test_pending_input_sleeps_minimum(void)
{
    assert(service_gui_wait_ms(16U, 0U, true, TEST_REFRESH_PERIOD_MS) == 1U);
    assert(service_gui_wait_ms(UINT32_MAX, 0U, true, TEST_REFRESH_PERIOD_MS) == 1U);
    assert(service_gui_wait_ms(0U, 40U, true, TEST_REFRESH_PERIOD_MS) == 1U);
}

/**
 * @brief 结果恒在 [1, 上限] 内；上限被误配成 0 时按下限处理。
 */
static void test_result_stays_within_bounds(void)
{
    for (uint32_t handler = 0U; handler < 64U; handler++)
    {
        for (uint32_t elapsed = 0U; elapsed < 64U; elapsed++)
        {
            uint32_t pending_wait = service_gui_wait_ms(handler, elapsed, true, TEST_REFRESH_PERIOD_MS);
            uint32_t wait = service_gui_wait_ms(handler, elapsed, false, TEST_REFRESH_PERIOD_MS);

            assert(pending_wait == SERVICE_GUI_WAIT_MIN_MS);
            assert(wait >= SERVICE_GUI_WAIT_MIN_MS);
            assert(wait <= TEST_REFRESH_PERIOD_MS);
        }
    }

    assert(service_gui_wait_ms(16U, 0U, false, 0U) == SERVICE_GUI_WAIT_MIN_MS);
}

/**
 * @brief 运行全部用例。
 * @return 全部断言通过时返回 0。
 */
int main(void)
{
    test_subtracts_round_time_from_handler_value();
    test_round_longer_than_handler_value_sleeps_minimum();
    test_clamps_to_refresh_period();
    test_pending_input_sleeps_minimum();
    test_result_stays_within_bounds();
    printf("gui_service_wait tests passed\n");
    return 0;
}
