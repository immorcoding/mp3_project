/**
 ******************************************************************************
 * @file    test_gui_service_sleep.c
 * @brief   GUI 每圈睡眠时长的主机行为测试。
 *
 * @details
 *          期望值来自 #190 的决定：时长 = lv_timer_handler() 的返回值 - 本圈耗时，
 *          夹在 1 ms 与刷新周期 16 ms 之间；输入槽里有待取走的命令时只睡下限。
 *          0xFFFFFFFF 是 LVGL 8 的 LV_NO_TIMER_READY（没有任何定时器待命）。
 ******************************************************************************
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "Service/gui/gui_service_sleep.h"

/** @brief LVGL 8 的 LV_NO_TIMER_READY：lv_timer_handler() 在没有定时器待命时的返回值。 */
#define LV_NO_TIMER_READY_VALUE 0xFFFFFFFFU

/**
 * @brief 睡眠时长是定时器返回值减去本圈耗时。
 */
static void test_sleep_subtracts_elapsed_from_timer_wait(void)
{
    assert(service_gui_sleep_ms(10U, 3U, false) == 7U);
    assert(service_gui_sleep_ms(16U, 5U, false) == 11U);
    assert(service_gui_sleep_ms(16U, 0U, false) == 16U);
}

/**
 * @brief 本圈耗时用完甚至超过定时器返回值时只睡下限 1 ms，不因无符号相减回绕成很长的睡眠。
 */
static void test_sleep_floors_at_one_ms_when_frame_used_up_the_wait(void)
{
    assert(service_gui_sleep_ms(16U, 15U, false) == 1U);
    assert(service_gui_sleep_ms(16U, 16U, false) == 1U);
    assert(service_gui_sleep_ms(16U, 20U, false) == 1U);
    assert(service_gui_sleep_ms(16U, 0xFFFFFFFFU, false) == 1U);
    assert(service_gui_sleep_ms(0U, 0U, false) == 1U);
    assert(service_gui_sleep_ms(0U, 7U, false) == 1U);
}

/**
 * @brief 定时器等得很久（含没有任何定时器待命）时最多睡一个刷新周期 16 ms。
 */
static void test_sleep_ceils_at_refresh_period_when_timer_is_far(void)
{
    assert(service_gui_sleep_ms(17U, 0U, false) == 16U);
    assert(service_gui_sleep_ms(30U, 5U, false) == 16U);
    assert(service_gui_sleep_ms(1000U, 3U, false) == 16U);
    assert(service_gui_sleep_ms(LV_NO_TIMER_READY_VALUE, 0U, false) == 16U);
    assert(service_gui_sleep_ms(LV_NO_TIMER_READY_VALUE, 5U, false) == 16U);
}

/**
 * @brief 输入槽里有待取走的命令时只睡下限 1 ms，让下一圈马上取走（松手到出声不多等一个刷新周期）。
 */
static void test_sleep_takes_floor_when_input_is_pending(void)
{
    assert(service_gui_sleep_ms(16U, 5U, false) == 11U);
    assert(service_gui_sleep_ms(16U, 5U, true) == 1U);
    assert(service_gui_sleep_ms(16U, 0U, true) == 1U);
    assert(service_gui_sleep_ms(1000U, 0U, true) == 1U);
    assert(service_gui_sleep_ms(LV_NO_TIMER_READY_VALUE, 0U, true) == 1U);
    assert(service_gui_sleep_ms(0U, 20U, true) == 1U);
}

/**
 * @brief 无论输入如何，睡眠时长恒在 1 到 16 ms 之间（Service_GUI_Process() 的返回合同）。
 */
static void test_sleep_always_within_one_to_sixteen_ms(void)
{
    static const uint32_t timer_values[] = {
        0U, 1U, 2U, 15U, 16U, 17U, 33U, 1000U, 0xFFFFFFFEU, LV_NO_TIMER_READY_VALUE};
    static const uint32_t elapsed_values[] = {
        0U, 1U, 5U, 15U, 16U, 17U, 36U, 1000U, 0xFFFFFFFFU};
    size_t timer_index;
    size_t elapsed_index;
    unsigned pending;

    for (timer_index = 0U;
         timer_index < sizeof(timer_values) / sizeof(timer_values[0]);
         timer_index++)
    {
        for (elapsed_index = 0U;
             elapsed_index < sizeof(elapsed_values) / sizeof(elapsed_values[0]);
             elapsed_index++)
        {
            for (pending = 0U; pending < 2U; pending++)
            {
                const uint32_t wait_ms = service_gui_sleep_ms(
                    timer_values[timer_index],
                    elapsed_values[elapsed_index],
                    pending != 0U);

                assert(wait_ms >= 1U);
                assert(wait_ms <= 16U);
            }
        }
    }
}

/**
 * @brief 运行全部睡眠时长测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_sleep_subtracts_elapsed_from_timer_wait();
    test_sleep_floors_at_one_ms_when_frame_used_up_the_wait();
    test_sleep_ceils_at_refresh_period_when_timer_is_far();
    test_sleep_takes_floor_when_input_is_pending();
    test_sleep_always_within_one_to_sixteen_ms();

    puts("gui_service_sleep_tests: all tests passed");
    return 0;
}
