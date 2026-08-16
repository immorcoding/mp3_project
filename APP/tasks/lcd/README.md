# LCD Task

本 Task 是当前 LCD 产品流程的暂时入口。LCD 与触摸已由 `Platform_Init()` 完成硬件初始化；任务通过 `Platform_Touch_ReadID()` 读取 Chip ID，随后使用一个位于 SDRAM 的整屏 RGB565 缓冲区，经 SPI DMA 顺序显示红、绿、蓝、白，以验证地址窗口、Cache、DMA、SPI EOT 和背光；不接入 LVGL。

## 公开 Interface

- `lcd_task(void *handle)`：仅由 `APP/tasks/app_tasks.c` 作为 FreeRTOS TaskFunction 创建。

## 编译期依赖

- `Platform_LCD_DrawPixel()`、`Platform_LCD_SetTransferCallback()`、`Platform_LCD_StartWrite()`；
- `Platform_Touch_ReadID()`；
- `LogService_Post()`。

## 运行时请求与事件路径

任务启动后先读取已完成启动初始化的触摸控制器 Chip ID，注册 `Platform_LCD_TransferCallback_t`，填满单帧 SDRAM 缓冲区并启动 DMA。SPI 最终 EOT 或错误经 Platform 回调进入任务通知索引 0；回调只调用 `xTaskNotifyIndexedFromISR()`，Task 被唤醒后再记录结果、填充下一帧。TP_IRQ 仅由 CubeMX 配置，尚未注册调用者回调。

当前整屏缓冲区是 DMA 基线验证，不是 ST7789 的显示双缓冲，也不能从根本消除扫描撕裂。接入 LVGL 后，GUI Task 应拥有两块“若干整行”的绘制缓冲区；DMA 在发送一块时，LVGL 可在另一块绘制，最终在 GUI Task 中调用 `lv_disp_flush_ready()`。

## 禁止依赖

不得包含 `spi.h`、`i2c.h`、`main.h`、STM32 HAL、ST7789/FT6X36 Component 或 Adapter 头文件；LCD/Touch 的具体总线、GPIO 和电源关系只能由各自 Platform Module 持有。

## 私有配置

`lcd_task_config.h` 保存纯色验收顺序使用的 RGB565 常量、保持时间和日志缓冲长度。它是本 Task 的临时硬件诊断参数，不是 Platform LCD 的通用绘制配置。

## 命名

Task 入口使用 `lcd_task()`；文件内私有 Implementation 使用 `lcd_task_*`。
