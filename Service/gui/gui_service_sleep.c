/**
  ******************************************************************************
  * @file    gui_service_sleep.c
  * @brief   GUI Task 每圈结束后睡多久的计算。
  *
  * @details
  *          只做整数运算，不包含 LVGL 与 FreeRTOS，主机测试直接覆盖。
  *          调用方（Service_GUI_Process）负责取 lv_timer_handler() 的返回值、
  *          用 FreeRTOS tick 量本圈耗时、读输入槽；睡眠本身由 GUI Task 的
  *          vTaskDelay 完成（GUI-28）。
  ******************************************************************************
  */

#include "Service/gui/gui_service_sleep.h"
#include "Service/gui/gui_service_config.h"

/**
 * @brief  算出本圈结束后 GUI Task 该睡的毫秒数。
 * @param[in] timer_ms lv_timer_handler() 的返回值，即距下一个 LVGL 定时器到期的毫秒数。
 *            0 表示已经到期；0xFFFFFFFF（LV_NO_TIMER_READY）表示没有定时器待命。
 *            这个值是按处理开始时的冻结时间算的：lv_tick 只在每圈开头推进，
 *            handler 运行期间不动，所以渲染耗时没有算在里面。
 * @param[in] elapsed_ms 本圈 Process 的实际耗时（FreeRTOS tick 换算成毫秒），要从 timer_ms 里扣掉，
 *            否则渲染耗时会叠加到帧间隔上。
 * @param[in] input_pending 输入槽里是否有尚未取走的命令；有则只睡下限，让下一圈马上取走。
 * @return 睡眠毫秒数，恒在 SERVICE_GUI_SLEEP_MIN_MS 到 SERVICE_GUI_SLEEP_MAX_MS 之间。
 * @note   不会因 elapsed_ms 大于 timer_ms 而无符号回绕。新结果到达不影响本函数：
 *         GUI 不提前唤醒，屏幕只按刷新周期更新。
 */
uint32_t service_gui_sleep_ms(
    uint32_t timer_ms,
    uint32_t elapsed_ms,
    bool input_pending)
{
    uint32_t remaining_ms;

    if (input_pending)
    {
        return SERVICE_GUI_SLEEP_MIN_MS;
    }

    remaining_ms = (timer_ms > elapsed_ms) ? (timer_ms - elapsed_ms) : 0U;

    if (remaining_ms < SERVICE_GUI_SLEEP_MIN_MS)
    {
        return SERVICE_GUI_SLEEP_MIN_MS;
    }

    if (remaining_ms > SERVICE_GUI_SLEEP_MAX_MS)
    {
        return SERVICE_GUI_SLEEP_MAX_MS;
    }

    return remaining_ms;
}
