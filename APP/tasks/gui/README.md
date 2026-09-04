# GUI Task

GUI Task 是 LVGL 的唯一执行上下文。它初始化 GUI Service，随后持续推进定时器、绘制与输入；SPI DMA 刷新期间的等待由 GUI Service 的 wait callback 完成。本目录不拥有 SquareLine 生成代码，也不修改 `GUI/`。

## 公开 Interface

- `gui_task(void *handle)`：仅由 `APP/tasks/app_tasks.c` 创建。
- `Gui_NotifyIndexTypeDef`：本任务私有通知槽。`GUI_NOTIFY_LCD_TRANSFER` 由 GUI Service 等待 LCD SPI DMA 完成；槽位编号只在本任务通知数组内有效，与 Storage Task 互不相关。

## 编译期依赖

- `Service_GUI_Init()` / `Service_GUI_Process()`；
- FreeRTOS Task 入口签名。

## 运行时请求与事件路径

任务启动时调用 `Service_GUI_Init(GUI_NOTIFY_LCD_TRANSFER)`；失败视为致命并进入 `Error_Handler()`。随后循环调用 `Service_GUI_Process()`。LCD DMA 最终 ISR 只通知本任务并调用 `lv_disp_flush_ready()`；其余 LVGL API 均在本 Task。

## 资源与约束

- 不创建第二个 LVGL 执行上下文；
- 不手改 `GUI/` 或 `SquareLineProject/`；
- 初始化失败没有部分回滚。

## 命名

任务入口使用 `gui_task()`；通知枚举使用 `GUI_NOTIFY_*`。
