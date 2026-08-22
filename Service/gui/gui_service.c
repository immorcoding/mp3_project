#include "gui_service.h"
#include "lvgl.h"

#include "Platform/lcd/platform_lcd.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include "stdint.h"

#define SERVICE_GUI_DRAW_BUFFER_LINES  40U

static uint8_t service_gui_draw_buffer_1[
    PLATFORM_LCD_WIDTH * SERVICE_GUI_DRAW_BUFFER_LINES * sizeof(lv_color_t)]
    __attribute__((section(".sdram_framebuffer"),
                   aligned(PLATFORM_DMA_BUFFER_ALIGNMENT)));

static uint8_t service_gui_draw_buffer_2[
    PLATFORM_LCD_WIDTH * SERVICE_GUI_DRAW_BUFFER_LINES * sizeof(lv_color_t)]
    __attribute__((section(".sdram_framebuffer"),
                   aligned(PLATFORM_DMA_BUFFER_ALIGNMENT)));

static lv_display_t *service_gui_display;

static uint32_t service_gui_get_tick_ms(void)
{
    return (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

void Service_GUI_Init(void)
{
    lv_init();
    lv_tick_set_cb(service_gui_get_tick_ms);

    service_gui_display = lv_display_create(PLATFORM_LCD_WIDTH,
                                        PLATFORM_LCD_HEIGHT);

    lv_display_set_buffers(service_gui_display,
                        service_gui_draw_buffer_1,
                        service_gui_draw_buffer_2,
                        sizeof(service_gui_draw_buffer_1),
                        LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void Service_GUI_Process(void)
{
    (void)lv_timer_handler();
}