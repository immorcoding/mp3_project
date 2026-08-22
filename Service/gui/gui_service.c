#include "Service/gui/gui_service.h"

#include <stdint.h>

#include "lvgl.h"

#include "Platform/lcd/platform_lcd.h"
#include "Platform/touch/platform_touch.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include "GUI/ui.h"

#define SERVICE_GUI_DRAW_BUFFER_LINES  320U

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

/**
  * @brief  向 LVGL 提供当前触摸状态。
  * @param  indev 当前 LVGL Pointer 输入设备。
  * @param  data LVGL 提供的输入采样输出。
  * @note   回调由 GUI Task 内的 LVGL 定时器调用。I2C 读取失败时主动报告释放，
  *         防止暂态通信错误使 LVGL 永久保留上一次按下状态。坐标当前直接使用
  *         控制器原始 X/Y；后续校准仅修改本函数，不向 Platform 下沉 UI 方向。
  */
static void service_gui_touch_read_callback(
    lv_indev_t *indev,
    lv_indev_data_t *data)
{
    Platform_Touch_RawPointTypeDef raw_point;

    (void)indev;
    data->state = LV_INDEV_STATE_RELEASED;

    if (Platform_Touch_ReadRawPoint(&raw_point) != PLATFORM_OK)
    {
        return;
    }

    if (!raw_point.IsPressed)
    {
        return;
    }

    if ((raw_point.X >= PLATFORM_LCD_WIDTH) ||
        (raw_point.Y >= PLATFORM_LCD_HEIGHT))
    {
        return;
    }

    data->point.x = (lv_coord_t)raw_point.X;
    data->point.y = (lv_coord_t)raw_point.Y;
    data->state = LV_INDEV_STATE_PRESSED;
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
    lv_indev_t *touch_indev;

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

    touch_indev = lv_indev_create();
    if (touch_indev == NULL)
    {
        return SERVICE_ERROR;
    }

    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(touch_indev, service_gui_display);
    lv_indev_set_read_cb(touch_indev,
                         service_gui_touch_read_callback);

    ui_init();

    return SERVICE_OK;
}

void Service_GUI_Process(void)
{
    (void)lv_timer_handler();
}
