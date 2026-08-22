#include "Service/gui/gui_service.h"

#include <stdint.h>

#include "lvgl.h"

#include "Platform/lcd/platform_lcd.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include "GUI/ui.h"

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

static void service_gui_lcd_transfer_callback(
    Platform_LCD_TransferEventTypeDef event,
    void *context)
{
    TaskHandle_t gui_task_handle = (TaskHandle_t)context;
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)event;

    if (gui_task_handle == NULL)
    {
        return;
    }

    vTaskNotifyGiveIndexedFromISR(
        gui_task_handle,
        FREERTOS_NOTIFY_INDEX_GUI_LCD_TRANSFER,
        &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void service_gui_flush_callback(
    lv_display_t *display,
    const lv_area_t *area,
    uint8_t *pixel_map)
{
    Platform_StatusTypeDef lcd_status;

    lcd_status = Platform_LCD_StartWrite(
        (uint16_t)area->x1,
        (uint16_t)area->y1,
        (uint16_t)area->x2,
        (uint16_t)area->y2,
        (const uint16_t *)pixel_map);

    if (lcd_status != PLATFORM_OK)
    {
        /* DMA 未启动，当前绘制缓冲可以立即归还 LVGL。 */
        lv_display_flush_ready(display);
    }
}

static void service_gui_flush_wait_callback(lv_display_t *display)
{
    (void)display;

    (void)ulTaskNotifyTakeIndexed(
        FREERTOS_NOTIFY_INDEX_GUI_LCD_TRANSFER,
        pdTRUE,
        portMAX_DELAY);
}

Service_StatusTypeDef Service_GUI_Init(void)
{
    if (service_gui_display != NULL)
    {
        return SERVICE_BUSY;
    }

    lv_init();
    lv_tick_set_cb(service_gui_get_tick_ms);

    service_gui_display = lv_display_create(PLATFORM_LCD_WIDTH,
                                            PLATFORM_LCD_HEIGHT);

    if (service_gui_display == NULL)
    {
        return SERVICE_ERROR;
    }

    Platform_StatusTypeDef lcd_status;
    lcd_status = Platform_LCD_SetTransferCallback(
        service_gui_lcd_transfer_callback,
        xTaskGetCurrentTaskHandle());
    
    if (lcd_status == PLATFORM_BUSY)
    {
        return SERVICE_BUSY;
    }

    if (lcd_status != PLATFORM_OK)
    {
        return SERVICE_ERROR;
    }

    lv_display_set_buffers(service_gui_display,
                           service_gui_draw_buffer_1,
                           service_gui_draw_buffer_2,
                           sizeof(service_gui_draw_buffer_1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_display_set_flush_cb(
        service_gui_display,
        service_gui_flush_callback);
    lv_display_set_flush_wait_cb(
        service_gui_display,
        service_gui_flush_wait_callback);

    ui_init();

    return SERVICE_OK;
}

void Service_GUI_Process(void)
{
    (void)lv_timer_handler();
}
