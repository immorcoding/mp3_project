/**
  ******************************************************************************
  * @file    platform_lcd_sim.c
  * @brief   Platform LCD 替身：把异步矩形写入同步拷进 SDL 帧缓冲。
  *
  * @details
  *          只实现 GUI Service 用到的 SetTransferCallback / StartWrite。写入在
  *          StartWrite 内同步完成，随即发布 TRANSFER_COMPLETE 最终事件，因此 LVGL
  *          的 wait_cb 不会进入阻塞等待。
  ******************************************************************************
  */

#include "Platform/lcd/platform_lcd.h"

#include <stddef.h>

#include "sim_display.h"

static Platform_LCD_TransferCallback_t sim_lcd_callback;
static void *sim_lcd_context;

Platform_StatusTypeDef Platform_LCD_SetTransferCallback(
    Platform_LCD_TransferCallback_t callback,
    void *context)
{
    sim_lcd_callback = callback;
    sim_lcd_context = context;
    return PLATFORM_OK;
}

Platform_StatusTypeDef Platform_LCD_StartWrite(uint16_t x_start,
                                               uint16_t y_start,
                                               uint16_t x_end,
                                               uint16_t y_end,
                                               const uint16_t *pixels)
{
    if ((pixels == NULL) ||
        (x_start > x_end) || (y_start > y_end) ||
        (x_end >= PLATFORM_LCD_WIDTH) || (y_end >= PLATFORM_LCD_HEIGHT))
    {
        return PLATFORM_LCD_ERROR;
    }

    sim_display_write(x_start, y_start, x_end, y_end, pixels);

    if (sim_lcd_callback != NULL)
    {
        sim_lcd_callback(PLATFORM_LCD_TRANSFER_COMPLETE, sim_lcd_context);
    }

    return PLATFORM_OK;
}
