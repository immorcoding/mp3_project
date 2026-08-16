/**
  ******************************************************************************
  * @file    lcd_task.c
  * @brief   LCD 最小绘制与触摸 Chip ID 诊断任务实现。
  *
  * @details
 *          LCD 与触摸控制器已由 Platform_Init() 在调度器启动前完成硬件初始化。
 *          本 Task 依次全屏显示红、绿、蓝、白，并读取触摸 Chip ID，用于验证
 *          RGB565、地址窗口、背光和 I2C2 通信链路。纯色测试使用 SDRAM 的
 *          单一整屏缓冲区和 SPI DMA 分块传输；它仍不接入 LVGL，后续 GUI 将
 *          另行拥有双绘制缓冲区并复用相同的 Platform LCD 异步 Interface。
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

/**
 * @brief LCD 亮屏测试的固定颜色顺序。
 * @details 白屏后的两次连续红屏用于诊断上一帧 GRAM 残留：红 A 若出现白色线段而
 *          紧随其后的红 B 正常，说明对应位置未被前一次红色传输覆盖。
 */
static const lcd_task_color_test_type lcd_task_color_tests[] = {
    {.Color = LCD_TASK_COLOR_WHITE, .Name = "white"},
    {.Color = LCD_TASK_COLOR_RED, .Name = "red A"},
    {.Color = LCD_TASK_COLOR_RED, .Name = "red B"},
    {.Color = LCD_TASK_COLOR_GREEN, .Name = "green"},
    {.Color = LCD_TASK_COLOR_BLUE, .Name = "blue"}
};

/**
 * @brief LCD DMA 纯色测试使用的单帧 RGB565 SDRAM 缓冲区。
 * @note  本对象位于链接脚本的 `.sdram_framebuffer (NOLOAD)` 段，启动时不清零。
 *        每次启动 DMA 前，`lcd_task_fill_dma_buffer()` 都会覆盖全部像素，因此
 *        不依赖其上电初值。大小 153600 B 与首地址均为 32 B 对齐，满足 M7
 *        D-Cache Clean 的完整 Cache line 约束。最终 DMA 通知到达前不得重写。
 */
static uint16_t lcd_task_dma_buffer[PLATFORM_LCD_WIDTH * PLATFORM_LCD_HEIGHT]
    __attribute__((section(".sdram_framebuffer"),
                   aligned(PLATFORM_DMA_BUFFER_ALIGNMENT)));

/**
 * @brief  覆盖 LCD Task 通知槽中残留的上一笔 DMA 事件。
 * @note   正常路径会在每次完成后清除通知值；此处额外清空可防止调试暂停、
 *         失败恢复等场景下的旧事件被误解释为新一帧完成。
 */
static void lcd_task_clear_transfer_events(void)
{
    uint32_t ignored_events;

    (void)xTaskNotifyWaitIndexed(FREERTOS_NOTIFY_INDEX_LCD_TRANSFER,
                                 0u,
                                 UINT32_MAX,
                                 &ignored_events,
                                 0u);
}

/**
 * @brief  把一个 RGB565 纯色写满 LCD DMA 单帧缓冲区。
 * @param  color 待写入的 RGB565 颜色。
 * @note   缓冲区在 SDRAM，无须占用 LCD Task 栈；Adapter 将在 DMA 启动前统一
 *         Clean D-Cache。本函数只会在上一笔传输已经报告最终事件后调用。
 */
static void lcd_task_fill_dma_buffer(uint16_t color)
{
    for (uint32_t index = 0u;
         index < (uint32_t)(sizeof(lcd_task_dma_buffer) /
                            sizeof(lcd_task_dma_buffer[0]));
         ++index)
    {
        lcd_task_dma_buffer[index] = color;
    }
}

/**
 * @brief  在 SPI ISR 上下文中把 LCD DMA 结果通知给所属 LCD Task。
 * @param  event Platform LCD 发布的最终传输事件。
 * @param  context 初始化时注册的 LCD Task Handle。
 * @note   本回调不执行绘制、日志或延时。SPI DMA 与 SPI EOT 均使用允许调用
 *         FreeRTOS FromISR Interface 的 NVIC 优先级；若本次唤醒了更高优先级
 *         任务，`portYIELD_FROM_ISR()` 请求 PendSV 立即切换。
 */
static void lcd_task_transfer_callback(Platform_LCD_TransferEventTypeDef event,
                                       void *context)
{
    TaskHandle_t task_handle = (TaskHandle_t)context;
    BaseType_t higher_priority_task_woken = pdFALSE;
    uint32_t events = (event == PLATFORM_LCD_TRANSFER_COMPLETE)
                          ? LCD_TASK_TRANSFER_EVENT_COMPLETE
                          : LCD_TASK_TRANSFER_EVENT_ERROR;

    if (task_handle != NULL)
    {
        (void)xTaskNotifyIndexedFromISR(task_handle,
                                        FREERTOS_NOTIFY_INDEX_LCD_TRANSFER,
                                        events,
                                        eSetBits,
                                        &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/**
 * @brief  启动一次全屏 SPI DMA 写入并等待其最终 EOT 或错误事件。
 * @retval true 全屏像素已完整移出 MOSI。
 * @retval false 启动被拒绝、SPI DMA 失败或 SPI EOT 报告错误。
 * @note   成功启动后可在进入阻塞前就收到 ISR 通知；任务通知会保留 pending
 *         状态，因此 `xTaskNotifyWaitIndexed()` 仍会立即返回，不存在丢失事件。
 */
static bool lcd_task_write_screen_dma(void)
{
    uint32_t events;

    lcd_task_clear_transfer_events();

    if (Platform_LCD_StartWrite(0u,
                                0u,
                                (uint16_t)(PLATFORM_LCD_WIDTH - 1u),
                                (uint16_t)(PLATFORM_LCD_HEIGHT - 1u),
                                lcd_task_dma_buffer) != PLATFORM_OK)
    {
        return false;
    }

    if (xTaskNotifyWaitIndexed(FREERTOS_NOTIFY_INDEX_LCD_TRANSFER,
                               0u,
                               UINT32_MAX,
                               &events,
                               portMAX_DELAY) != pdTRUE)
    {
        return false;
    }

    return ((events & LCD_TASK_TRANSFER_EVENT_COMPLETE) != 0u) &&
           ((events & LCD_TASK_TRANSFER_EVENT_ERROR) == 0u);
}

/**
 * @brief  顺序执行白、红 A、红 B、绿、蓝全屏 DMA 填充，并验证一次单点写入路径。
 * @retval true 所有纯色均已成功写入，最终停留在白屏。
 * @retval false 任一地址窗口、SPI DMA 或 SPI EOT 操作失败。
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
        lcd_task_fill_dma_buffer(lcd_task_color_tests[index].Color);

        if (!lcd_task_write_screen_dma())
        {
            return false;
        }

        (void)snprintf(message,
                       sizeof(message),
                       "DMA color test: %s.",
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
 * @brief  验证触摸 Chip ID 并执行 RGB565 SPI DMA 亮屏测试。
  * @param  handle 未使用，保留以满足 FreeRTOS TaskFunction_t。
 * @note   LCD Task 不直接包含 HAL SPI/GPIO 头文件。具体 GPIO、SPI1、电源和
 *         控制器初始化由 Platform Init 完成；本 Task 只调用已就绪的产品能力、
 *         执行纯色测试并投递日志。
  * @retval None
  */
void lcd_task(void *handle)
{
    TaskHandle_t task_handle;
    bool transfer_callback_registered;

    (void)handle;

    lcd_task_log_touch_id();

    task_handle = xTaskGetCurrentTaskHandle();
    transfer_callback_registered =
        (Platform_LCD_SetTransferCallback(lcd_task_transfer_callback,
                                          task_handle) == PLATFORM_OK);

    for (;;)
    {
        if (!transfer_callback_registered)
        {
            (void)LogService_Post(LOG_LEVEL_ERROR,
                                  "LCD",
                                  "DMA callback registration failed; retrying.");
            vTaskDelay(pdMS_TO_TICKS(LCD_TASK_CALLBACK_RETRY_PERIOD_MS));
            transfer_callback_registered =
                (Platform_LCD_SetTransferCallback(lcd_task_transfer_callback,
                                                  task_handle) == PLATFORM_OK);
            continue;
        }

        /* 持续刷屏 DMA 测试。 */
        vTaskDelay(pdMS_TO_TICKS(LCD_TASK_REFRESH_PERIOD_MS));

        if (lcd_task_run_color_test())
        {
            (void)LogService_Post(LOG_LEVEL_INFO,
                                   "LCD",
                                   "DMA color test completed; final screen is white.");
        }
        else
        {
            (void)LogService_Post(LOG_LEVEL_ERROR, "LCD", "DMA color test failed.");
        }
    }
}
