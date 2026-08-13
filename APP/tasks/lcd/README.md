# LCD Task

本 Task 是当前 LCD 产品流程的暂时入口。任务启动时调用 `Platform_LCD_Init()` 与 `Platform_LCD_ReadID()`，随后顺序全屏显示红、绿、蓝、白，以验证 RGB565、地址窗口、SPI 和背光；不接入 DMA 或 LVGL。

## 公开 Interface

- `lcd_task(void *handle)`：仅由 `APP/tasks/app_tasks.c` 作为 FreeRTOS TaskFunction 创建。

## 编译期依赖

- `Platform_LCD_Init()`、`Platform_LCD_ReadID()`；
- `LogService_Post()`。

## 运行时请求与事件路径

任务启动后调用 Platform LCD 的阻塞诊断与纯色填屏能力，再经 LogService 投递结果；当前不存在 LCD ISR、DMA 或 LVGL flush 事件。

## 禁止依赖

不得包含 `spi.h`、`main.h`、STM32 HAL、ST7789 Component 或 Adapter 头文件；LCD 的具体 SPI、GPIO 和电源关系只能由 Platform LCD 持有。

## 私有配置

`lcd_task_config.h` 保存纯色验收顺序使用的 RGB565 常量、保持时间和日志缓冲长度。它是本 Task 的临时硬件诊断参数，不是 Platform LCD 的通用绘制配置。

## 命名

Task 入口使用 `lcd_task()`；文件内私有 Implementation 使用 `lcd_task_*`。
