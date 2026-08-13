/**
  ******************************************************************************
  * @file    lcd_task.c
  * @brief   LCD 最小 RDDID 诊断任务实现。
  *
  * @details
 *          当前 Task 执行一次 LCD 电源、控制器初始化和 RDDID 读取，随后依次
 *          全屏显示红、绿、蓝、白，用于验证 RGB565、地址窗口和背光链路。它
 *          不接入 DMA、帧缓冲或 LVGL；后续显示循环将在这个成功基线之上扩展。
  ******************************************************************************
  */

#include "APP/tasks/lcd/lcd_task.h"
#include "APP/tasks/lcd/lcd_task_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "Components/log/log.h"
#include "Platform/lcd/platform_lcd.h"
#include "Service/log/log_service.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

// uint16_t frame_buffer[200] __attribute__((section(".sdram_data")));	//test only, for SDRAM test, not used in LCD task

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
 * @brief  开启 LCD、读取 ST7789 RDDID 并执行 RGB565 亮屏测试。
  * @param  handle 未使用，保留以满足 FreeRTOS TaskFunction_t。
  * @note   LCD Task 不直接包含 HAL SPI/GPIO 头文件。具体 GPIO、SPI1 和电源
 *         装配由 Platform LCD 完成；本 Task 只解释产品级成功或失败、调用纯色
 *         测试能力并投递日志。
  * @retval None
  */
void lcd_task(void *handle)
{
    Platform_LCD_IDTypeDef id;
    char message[LCD_TASK_LOG_MESSAGE_LENGTH];

    (void)handle;

    if (Platform_LCD_Init() != PLATFORM_OK)
    {
        (void)LogService_Post(LOG_LEVEL_ERROR,
                               "LCD",
                               "Initialization or reset failed.");
    }
    else if (Platform_LCD_ReadID(&id) != PLATFORM_OK)
    {
        (void)LogService_Post(LOG_LEVEL_ERROR, "LCD", "RDDID read failed.");
    }
    else
    {
        (void)snprintf(message,
                       sizeof(message),
                       "RDDID: %02X %02X %02X.",
                       (unsigned int)id.ID1,
                       (unsigned int)id.ID2,
                       (unsigned int)id.ID3);
        (void)LogService_Post(LOG_LEVEL_INFO, "LCD", message);

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

    for (;;)
    {
        /* 保留 Task 实例供后续显示状态机接管，当前诊断完成后不占用 CPU。 */
        vTaskDelay(pdMS_TO_TICKS(LCD_TASK_IDLE_PERIOD_MS));
    }
}
