/**
  ******************************************************************************
  * @file    lcd_task.c
  * @brief   LCD 最小绘制与触摸 Chip ID 诊断任务实现。
  *
  * @details
 *          LCD 与触摸控制器已由 Platform_Init() 在调度器启动前完成硬件初始化。
 *          本 Task 依次全屏显示红、绿、蓝、白，并读取触摸 Chip ID，用于验证
 *          RGB565、地址窗口、背光和 I2C2 通信链路。它不接入 DMA、帧缓冲或
 *          LVGL；后续显示循环将在这个成功基线之上扩展。
  ******************************************************************************
  */

#include "APP/tasks/lcd/lcd_task.h"
#include "APP/tasks/lcd/lcd_task_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "Components/log/log.h"
#include "Platform/lcd/platform_lcd.h"
#include "Platform/touch/platform_touch.h"
#include "Service/log/log_service.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/** @brief 一项 LCD 全屏纯色测试的颜色值和可读名称。 */
typedef struct
{
    uint16_t Color;      /**< 待写入全屏的 RGB565 颜色。 */
    const char *Name;    /**< 用于日志的静态颜色名称。 */
} lcd_task_color_test_type;

/** @brief LCD 亮屏测试的固定颜色顺序。 */
static const lcd_task_color_test_type lcd_task_color_tests[] = {
    {.Color = LCD_TASK_COLOR_RED, .Name = "red"},
    {.Color = LCD_TASK_COLOR_GREEN, .Name = "green"},
    {.Color = LCD_TASK_COLOR_BLUE, .Name = "blue"},
    {.Color = LCD_TASK_COLOR_WHITE, .Name = "white"}
};

/**
 * @brief  顺序执行红、绿、蓝、白全屏填充，并验证一次单点写入路径。
 * @retval true 所有纯色均已成功写入，最终停留在白屏。
 * @retval false 任一 SPI/地址窗口写入失败。
 * @note   每种颜色保持一秒，便于肉眼检查颜色通道、背光和扫描方向。最后向
 *         左上角写一个黑像素，仅验证 `Platform_LCD_DrawPixel()` 路径；该点不
 *         作为肉眼可见的主要验收项。
 */
static bool lcd_task_run_color_test(void)
{
    char message[LCD_TASK_LOG_MESSAGE_LENGTH];

    for (uint32_t index = 0u;
         index < (sizeof(lcd_task_color_tests) / sizeof(lcd_task_color_tests[0]));
         ++index)
    {
        if (Platform_LCD_FillScreen(lcd_task_color_tests[index].Color) != PLATFORM_OK)
        {
            return false;
        }

        (void)snprintf(message,
                       sizeof(message),
                       "Color test: %s.",
                       lcd_task_color_tests[index].Name);
        (void)LogService_Post(LOG_LEVEL_INFO, "LCD", message);
        vTaskDelay(pdMS_TO_TICKS(LCD_TASK_COLOR_HOLD_PERIOD_MS));
    }

    return Platform_LCD_DrawPixel(0u, 0u, LCD_TASK_COLOR_BLACK) == PLATFORM_OK;
}

/**
  * @brief  读取并记录已完成启动初始化的触摸控制器 Chip ID。
  * @note   TP_RST 复位和 I2C 地址探测已由 Platform_Init() 在调度器启动前完成。
  *         当前仅验证 FT6X36 最小硬件通信链路，不解释触点坐标、不调用 LVGL，
  *         也不使用 TP_IRQ。后续 LVGL 输入回调将在同一 LCD Task 中扩展轮询读取。
  */
static void lcd_task_log_touch_id(void)
{
    uint8_t chip_id;
    char message[LCD_TASK_LOG_MESSAGE_LENGTH];

    if (Platform_Touch_ReadID(&chip_id) != PLATFORM_OK)
    {
        (void)LogService_Post(LOG_LEVEL_ERROR,
                              "TOUCH",
                              "Chip ID read failed after startup initialization.");
        return;
    }

    (void)snprintf(message, sizeof(message), "Chip ID: 0x%02X.",
                   (unsigned int)chip_id);
    (void)LogService_Post(LOG_LEVEL_INFO, "TOUCH", message);
}

/**
 * @brief  验证触摸 Chip ID 并执行 RGB565 亮屏测试。
  * @param  handle 未使用，保留以满足 FreeRTOS TaskFunction_t。
 * @note   LCD Task 不直接包含 HAL SPI/GPIO 头文件。具体 GPIO、SPI1、电源和
 *         控制器初始化由 Platform Init 完成；本 Task 只调用已就绪的产品能力、
 *         执行纯色测试并投递日志。
  * @retval None
  */
void lcd_task(void *handle)
{
    (void)handle;

    lcd_task_log_touch_id();

    for (;;)
    {
        /* 持续刷屏测试。 */
        vTaskDelay(pdMS_TO_TICKS(LCD_TASK_REFRESH_PERIOD_MS));

        if (lcd_task_run_color_test())
        {
            (void)LogService_Post(LOG_LEVEL_INFO,
                                   "LCD",
                                   "Color test completed; final screen is white.");
        }
        else
        {
            (void)LogService_Post(LOG_LEVEL_ERROR, "LCD", "Color test failed.");
        }
    }
}
