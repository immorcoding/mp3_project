# LCD Task

本 Task 是当前 LCD 产品流程的暂时入口。当前只在任务启动时调用 `Platform_LCD_Init()` 与 `Platform_LCD_ReadID()`，将一次性诊断结果投递到 LogService；不执行显示初始化、像素刷新、DMA 或 LVGL 工作。

## 公开 Interface

- `lcd_task(void *handle)`：仅由 `APP/tasks/app_tasks.c` 作为 FreeRTOS TaskFunction 创建。

## 编译期依赖

- `Platform_LCD_Init()`、`Platform_LCD_ReadID()`；
- `LogService_Post()`。

## 运行时请求与事件路径

任务启动后调用 Platform LCD 的阻塞诊断能力，再经 LogService 投递一次结果；当前不存在 LCD ISR、DMA 或 LVGL flush 事件。

## 禁止依赖

不得包含 `spi.h`、`main.h`、STM32 HAL、ST7789 Component 或 Adapter 头文件；LCD 的具体 SPI、GPIO 和电源关系只能由 Platform LCD 持有。

## 命名

Task 入口使用 `lcd_task()`；文件内私有 Implementation 使用 `lcd_task_*`。
