/**
  ******************************************************************************
  * @file    lcd_task.c
  * @brief   LCD 最小 RDDID 诊断任务实现。
  *
  * @details
  *          当前 Task 只执行一次 LCD 电源、硬件复位和 RDDID 读取，并将结果
  *          投递给 LogService。它不初始化显示寄存器、不打开背光、不接入 DMA
  *          或 LVGL；后续显示循环将在这个明确的成功基线之上扩展。
  ******************************************************************************
  */

#include "APP/tasks/lcd/lcd_task.h"

#include <stdio.h>

#include "Components/log/log.h"
#include "Platform/lcd/platform_lcd.h"
#include "Service/log/log_service.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/** @brief LCD 诊断完成后的低频空闲周期，单位为毫秒。 */
#define LCD_TASK_IDLE_PERIOD_MS     1000u

/** @brief 单条 LCD 诊断日志的本地格式化缓冲区长度。 */
#define LCD_TASK_LOG_MESSAGE_LENGTH 64u

/**
  * @brief  开启 LCD 最小链路并投递一次 ST7789 RDDID 读取结果。
  * @param  handle 未使用，保留以满足 FreeRTOS TaskFunction_t。
  * @note   LCD Task 不直接包含 HAL SPI/GPIO 头文件。具体 GPIO、SPI1 和电源
  *         装配由 Platform LCD 完成；本 Task 只解释产品级成功或失败并投递日志。
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
    }

    for (;;)
    {
        /* 保留 Task 实例供后续显示状态机接管，当前诊断完成后不占用 CPU。 */
        vTaskDelay(pdMS_TO_TICKS(LCD_TASK_IDLE_PERIOD_MS));
    }
}
