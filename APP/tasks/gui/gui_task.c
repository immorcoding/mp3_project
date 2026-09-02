/**
  ******************************************************************************
  * @file    gui_task.c
  * @brief   GUI Task 的 FreeRTOS 运行循环。
  *
  * @details
  *          本任务是 LVGL 的唯一执行上下文。它先初始化 GUI Service，随后持续
  *          推进 LVGL 定时器、绘制与输入处理；SPI DMA 刷新期间的等待由 GUI
  *          Service 注册给 LVGL 的 wait callback 完成。
  ******************************************************************************
  */

#include "gui_task.h"

#include "Service/gui/gui_service.h"
#include "main.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/**
 * @brief  初始化 GUI Service 并持续推进唯一的 LVGL 执行上下文。
 * @param  handle 未使用；保留以符合 FreeRTOS TaskFunction_t 签名。
 * @note   GUI 初始化失败被当前产品定义为致命错误，任务会进入 Error_Handler()。
 *         正常路径不返回，所有 LVGL API（LCD 最终 ISR 的 flush ready 例外除外）
 *         均由本 Task 调用。
 */
void gui_task(void *handle)
{
    (void)handle;

    _Static_assert((unsigned)GUI_NOTIFY_COUNT <=
                       (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES,
                   "GUI Task notify slots exceed FreeRTOS array length");

    if (Service_GUI_Init(GUI_NOTIFY_LCD_TRANSFER) != SERVICE_OK)
    {
        Error_Handler();
    }

    while (1)
    {
        Service_GUI_Process();
    }
}
