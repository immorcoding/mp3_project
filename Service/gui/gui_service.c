/**
  ******************************************************************************
  * @file    gui_service.c
  * @brief   LVGL v8 显示、触摸与 SPI DMA 刷新的 Service 装配实现。
  *
  * @details
  *          本 Module 在唯一 GUI Task 中持有 LVGL 显示/输入驱动和两块 SDRAM
  *          绘制缓冲。它把 LVGL flush/wait callback 映射为 Platform LCD 异步
  *          写入与 FreeRTOS 任务通知，并把 Platform Touch 原始触点提供给
  *          LVGL Pointer 输入驱动。
  ******************************************************************************
  */

#include "Service/gui/gui_service.h"
#include "Service/gui/boot/gui_service_boot.h"
#include "Service/gui/gui_service_config.h"
#include "Service/gui/main/gui_service_main.h"
#include "Service/gui/main/queue/gui_service_main_queue.h"
#include "Service/gui/theme/gui_service_theme_apply.h"

#include <stdint.h>

#include "lvgl.h"

#include "Platform/lcd/platform_lcd.h"
#include "Platform/touch/platform_touch.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include "GUI/ui.h"

/**
 * @brief LVGL v8 仅保存两块绘制缓冲的指针，故其存储期必须覆盖显示驱动的整个生命周期。
 */
static lv_color_t service_gui_draw_buffer_1[
    PLATFORM_LCD_WIDTH * SERVICE_GUI_DRAW_BUFFER_LINES]
    __attribute__((section(".sdram_framebuffer"),
                   aligned(PLATFORM_DMA_BUFFER_ALIGNMENT)));

static lv_color_t service_gui_draw_buffer_2[
    PLATFORM_LCD_WIDTH * SERVICE_GUI_DRAW_BUFFER_LINES]
    __attribute__((section(".sdram_framebuffer"),
                   aligned(PLATFORM_DMA_BUFFER_ALIGNMENT)));

static lv_disp_draw_buf_t service_gui_draw_buffer;
static lv_disp_drv_t service_gui_display_driver;
static lv_indev_drv_t service_gui_touch_driver;
static lv_disp_t *service_gui_display;
static TickType_t service_gui_last_tick;
static uint32_t service_gui_notify_index;

/**
  * @brief  向 LVGL 提供当前触摸状态。
  * @param  indev_drv 当前 LVGL Pointer 输入驱动。
  * @param  data LVGL 提供的输入采样输出。
  * @note   回调由 GUI Task 内的 LVGL 定时器调用。I2C 读取失败时主动报告释放，
  *         防止暂态通信错误使 LVGL 永久保留上一次按下状态。坐标当前直接使用
  *         控制器原始 X/Y；后续校准仅修改本函数，不向 Platform 下沉 UI 方向。
  */
static void service_gui_touch_read_callback(
    lv_indev_drv_t *indev_drv,
    lv_indev_data_t *data)
{
    Platform_Touch_RawPointTypeDef raw_point;

    (void)indev_drv;
    data->state = LV_INDEV_STATE_RELEASED;

    /* 触摸初始化或上一次读取失败后，不再触发 I2C 轮询。 */
    if (!Platform_Touch_IsAvailable())
    {
        return;
    }

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

    /*
     * Platform LCD 只在 SPI EOT 或错误后发布最终事件。此时缓冲已经不再被 LCD DMA
     * 使用，才可通知 LVGL 归还本次 flush 的绘制缓冲。
     */
    lv_disp_flush_ready(&service_gui_display_driver);

    if (gui_task_handle == NULL)
    {
        return;
    }

    vTaskNotifyGiveIndexedFromISR(
        gui_task_handle,
        (UBaseType_t)service_gui_notify_index,
        &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void service_gui_flush_callback(
    lv_disp_drv_t *disp_drv,
    const lv_area_t *area,
    lv_color_t *color_p)
{
    Platform_StatusTypeDef lcd_status;

    lcd_status = Platform_LCD_StartWrite(
        (uint16_t)area->x1,
        (uint16_t)area->y1,
        (uint16_t)area->x2,
        (uint16_t)area->y2,
        (const uint16_t *)color_p);

    if (lcd_status != PLATFORM_OK)
    {
        /* DMA 未启动，当前绘制缓冲可以立即归还 LVGL。 */
        lv_disp_flush_ready(disp_drv);
    }
}

/**
  * @brief  在 GUI Task 中等待当前 SPI DMA 刷新结束。
  * @param  disp_drv 当前 LVGL 显示驱动。
  * @note   只有 LVGL 已标记绘制缓冲为 flushing 时才会进入本回调。DMA 最终事件
  *         由 ISR 同时调用 lv_disp_flush_ready() 并发送任务通知。
  */
static void service_gui_flush_wait_callback(
    lv_disp_drv_t *disp_drv)
{
    (void)disp_drv;

    (void)ulTaskNotifyTakeIndexed(
        (UBaseType_t)service_gui_notify_index,
        pdTRUE,
        portMAX_DELAY);
}

/**
 * @brief  初始化 GUI Task 独占的 LVGL、显示、触摸与启动视觉序列。
 * @param[in] notify_index GUI Task 通知数组中的 LCD DMA 完成槽，由 APP 枚举注入。
 * @retval SERVICE_OK 全部 LVGL Driver、LCD 最终回调、Pointer 输入和 Boot 资源已就绪。
 * @retval SERVICE_BUSY GUI 已初始化，或 Platform LCD 正在使用其唯一最终回调。
 * @retval SERVICE_INVALID_PARAM 通知槽越界，或壁纸资源不满足当前 Canvas 视觉处理约束。
 * @retval SERVICE_ERROR 显示、输入或启动视觉资源的注册/生成失败。
 * @retval SERVICE_NOT_READY GUI 生成对象或内部 Canvas 尚未就绪。
 * @note   只能由 GUI Task 调用一次。SquareLine `ui_init()` 会 `lv_theme_basic_init`
 *         覆盖 display theme，因此占位色过滤器必须在 `ui_init()` 之后重新挂上，
 *         再对已创建 Screen 整树绑定。随后对 STARTUP 外观调用 ThemeApply，再准备
 *         Main 与 Boot。
 *         当前 GUI Task 将任何非 SERVICE_OK 视为致命初始化故障并进入
 *         Error_Handler()；本 Module 尚未提供失败后的回滚或重试。
 */
Service_StatusTypeDef Service_GUI_Init(uint32_t notify_index)
{
    Platform_StatusTypeDef lcd_status;
    Service_StatusTypeDef gui_status;

    if (notify_index >= (uint32_t)configTASK_NOTIFICATION_ARRAY_ENTRIES)
    {
        return SERVICE_INVALID_PARAM;
    }

    if (service_gui_display != NULL)
    {
        return SERVICE_BUSY;
    }

    service_gui_notify_index = notify_index;

    lv_init();

    lv_disp_draw_buf_init(
        &service_gui_draw_buffer,
        service_gui_draw_buffer_1,
        service_gui_draw_buffer_2,
        PLATFORM_LCD_WIDTH * SERVICE_GUI_DRAW_BUFFER_LINES);

    lv_disp_drv_init(&service_gui_display_driver);
    service_gui_display_driver.hor_res = PLATFORM_LCD_WIDTH;
    service_gui_display_driver.ver_res = PLATFORM_LCD_HEIGHT;
    service_gui_display_driver.draw_buf = &service_gui_draw_buffer;
    service_gui_display_driver.flush_cb = service_gui_flush_callback;
    service_gui_display_driver.wait_cb = service_gui_flush_wait_callback;

    service_gui_display = lv_disp_drv_register(&service_gui_display_driver);

    if (service_gui_display == NULL)
    {
        return SERVICE_ERROR;
    }

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

    lv_indev_drv_init(&service_gui_touch_driver);
    service_gui_touch_driver.type = LV_INDEV_TYPE_POINTER;
    service_gui_touch_driver.disp = service_gui_display;
    service_gui_touch_driver.read_cb = service_gui_touch_read_callback;

    if (lv_indev_drv_register(&service_gui_touch_driver) == NULL)
    {
        return SERVICE_ERROR;
    }

    service_gui_last_tick = xTaskGetTickCount();
    ui_init();

    gui_status = service_gui_theme_attach(service_gui_display);
    if (gui_status != SERVICE_OK)
    {
        return gui_status;
    }

    service_gui_theme_bind_screens();

    gui_status = Service_GUI_ThemeApply(SERVICE_GUI_THEME_STARTUP);
    if (gui_status != SERVICE_OK)
    {
        return gui_status;
    }

    gui_status = service_gui_main_prepare(
        &ui_img_wallpaper_indigo_mist_soft_dark_png);

    if (gui_status != SERVICE_OK)
    {
        return gui_status;
    }

    service_gui_theme_bind_screens();

    gui_status = service_gui_boot_prepare_background(
        &ui_img_wallpaper_indigo_mist_soft_dark_png);

    if (gui_status != SERVICE_OK)
    {
        return gui_status;
    }

    /*
     * ui_init() 内部已经加载 Boot Screen，不能再依赖其 SCREEN_LOADED 事件来启动
     * Service 持有的动画。后续动画在此处显式启动，确保背景资源已完成运行时绑定。
     */
    service_gui_boot_start();

    /* Init 期间没有跑 timer_handler，不能把这段墙钟一次性灌进 lv_tick，
     * 否则 Boot 的切屏延迟会在第一圈 Process 立刻到期。 */
    service_gui_last_tick = xTaskGetTickCount();

    return SERVICE_OK;
}

/**
 * @brief  推进 GUI Task 的 LVGL Tick、输入、动画与刷新处理。
 * @note   只能由完成 Service_GUI_Init() 的同一 GUI Task 周期调用。LCD DMA 传输
 *         期间，LVGL 通过本 Module 的 wait callback 阻塞等待最终 ISR 事件。
 */
void Service_GUI_Process(void)
{
    const TickType_t current_tick = xTaskGetTickCount();
    const TickType_t elapsed_ticks = current_tick - service_gui_last_tick;

    if (elapsed_ticks != 0U)
    {
        /* TickType_t 无符号回绕的减法仍能得到两次调用之间的正确间隔。 */
        lv_tick_inc((uint32_t)elapsed_ticks * (uint32_t)portTICK_PERIOD_MS);
        service_gui_last_tick = current_tick;
    }

    (void)lv_timer_handler();
}

/**
 * @brief 读取 QueueTab 顶部已滚出的整行数，供 GUI Task 计算下一窗 Index。
 * @return 完整滚出顶部的行数；对象未就绪或尚无行时为 0。
 * @note 不包含 storage_listbuffer.h。只能由同一 GUI Task 调用。
 */
uint16_t Service_GUI_QueueScrollLead(void)
{
    return service_gui_main_queue_scroll_lead();
}

/**
 * @brief 取走一次 Queue 行点按对应的播放列表下标。
 * @param[out] sheet_index 被点行对应的播放列表下标。
 * @retval SERVICE_OK 有一次待处理点击。
 * @retval SERVICE_NOT_READY 没有待处理点击。
 * @retval SERVICE_INVALID_PARAM sheet_index 为空。
 * @note 只能由同一 GUI Task 调用。不包含 storage_listbuffer.h。
 *       点击发生在 Process() 的 LVGL 调度里，下一圈循环再 Consume。
 */
Service_StatusTypeDef Service_GUI_QueueConsumeSelect(uint16_t *sheet_index)
{
    return service_gui_main_queue_consume_select(sheet_index);
}

/**
 * @brief 把一窗曲名填进 Queue 可见行，不包含 storage_listbuffer。
 * @param[in] titles 曲名字符串指针表；length 为 0 时允许为 NULL。
 * @param[in] length 本窗实际条数，至多 SERVICE_GUI_MAIN_QUEUE_MAX_ROWS。
 * @param[in] window_index 本窗在播放列表上的起点，用于转 head。
 * @param[in] current_index 正在播放的播放列表下标；无当前曲时为
 *            SERVICE_GUI_QUEUE_NO_CURRENT。
 * @retval SERVICE_OK 已按 Length 显示，或 Length 为 0 已全部 Hidden。
 * @retval SERVICE_INVALID_PARAM Length 超上限，或 Length 非 0 但 titles 为空。
 * @retval SERVICE_NOT_READY Queue 范本尚未准备。
 * @retval SERVICE_ERROR 补造行时 LVGL 未能创建对象。
 * @note 只能由完成 Service_GUI_Init() 的同一 GUI Task 调用。曲名由调用方持有，
 *       Label 会拷贝文本，调用返回后调用方可把 listbuffer 写回 IDLE。
 *       已有行转 head 回收，不按整表无限 create。
 */
Service_StatusTypeDef Service_GUI_QueueApply(
    const char **titles,
    uint16_t length,
    uint16_t window_index,
    uint16_t current_index)
{
    return service_gui_main_queue_apply(titles, length, window_index, current_index);
}
