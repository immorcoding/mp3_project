/**
  ******************************************************************************
  * @file    gui_service_wait.c
  * @brief   GUI 每圈结束后的睡眠时长计算（#190 的规则 1 至 3）。
  ******************************************************************************
  */

#include "Service/gui/gui_service_wait.h"

/**
 * @brief 计算 GUI 本圈结束后应睡多久。
 * @param[in] handler_ms `lv_timer_handler()` 的返回值：距下一个 LVGL 定时器到期的毫秒数；
 *            没有定时器待办时 LVGL 返回一个极大值，按上限处理。
 * @param[in] elapsed_ms 本圈从 `lv_tick` 推进到 handler 返回之间实际经过的毫秒数。
 *            `lv_tick` 只在每圈开头推进，handler 运行期间时间不动，所以返回值要扣掉它。
 * @param[in] input_pending 输入槽里是否有尚未取走的命令；有则只睡下限，保持松手到出声的估算。
 * @param[in] max_ms 睡眠上限，通常是 LVGL 刷新周期；小于下限时按下限处理。
 * @return 睡眠毫秒数，恒在 [SERVICE_GUI_WAIT_MIN_MS, max_ms] 内。
 * @note  新结果到达不提前唤醒，所以这里没有结果参数。
 */
uint32_t service_gui_wait_ms(uint32_t handler_ms,
                             uint32_t elapsed_ms,
                             bool input_pending,
                             uint32_t max_ms)
{
    uint32_t wait_ms;

    if (max_ms < SERVICE_GUI_WAIT_MIN_MS)
    {
        max_ms = SERVICE_GUI_WAIT_MIN_MS;
    }

    if (input_pending)
    {
        return SERVICE_GUI_WAIT_MIN_MS;
    }

    wait_ms = (handler_ms > elapsed_ms) ? (handler_ms - elapsed_ms) : 0U;
    if (wait_ms < SERVICE_GUI_WAIT_MIN_MS)
    {
        wait_ms = SERVICE_GUI_WAIT_MIN_MS;
    }
    if (wait_ms > max_ms)
    {
        wait_ms = max_ms;
    }

    return wait_ms;
}
