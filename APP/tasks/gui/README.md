# GUI Task

GUI Task 是 LVGL 的唯一执行上下文。它初始化 GUI Service，随后持续推进定时器、绘制与输入；SPI DMA 刷新期间的等待由 GUI Service 的 wait callback 完成。本目录不拥有 SquareLine 生成代码，也不修改 `GUI/`。

同一循环里消费 `storage_listbuffer` 单槽窗口：SD **就绪**且 `IDLE` 时 `request` 从播放列表 `Index = 0` 起至多 8 条；看见 `READY` 后把窗口文本交给 `Service_GUI_QueueApply()`，成功后再记已展示，再把 `Status` 写回 `IDLE`。`request` 只带起点/条数/代次。当前 `Buffer` 仍是路径，原样显示；标题/歌手等 `load` 调解析器，见 `docs/catalog_architecture.md` 第 5 节。QueueApply 失败保持 `need_window`，写回 IDLE 后重试。清空只看 **卷仍挂载**，不把卡检测消抖当成拔卡。`Service/gui` 仍不得包含 `storage_listbuffer.h`。协议状态机在 `gui_task_queue_window.c`，不阻塞等待 READY。

## 公开 Interface

- `gui_task(void *handle)`：仅由 `APP/tasks/app_tasks.c` 创建。
- `Gui_NotifyIndexTypeDef`：本任务私有通知槽。`GUI_NOTIFY_LCD_TRANSFER` 由 GUI Service 等待 LCD SPI DMA 完成；槽位编号只在本任务通知数组内有效，与 Storage Task 互不相关。

## 编译期依赖

- `Service_GUI_Init()` / `Service_GUI_Process()` / `Service_GUI_QueueApply()`；
- `storage_listbuffer_*()`、`storage_task_sd_is_ready()` 与 `storage_task_sd_is_mounted()`；
- FreeRTOS Task 入口签名。

## 运行时请求与事件路径

任务启动时调用 `Service_GUI_Init(GUI_NOTIFY_LCD_TRANSFER)`；失败视为致命并进入 `Error_Handler()`。随后循环：窗口状态机 `request` / `QueueApply` / 清空，再调用 `Service_GUI_Process()`。LCD DMA 最终 ISR 只通知本任务并调用 `lv_disp_flush_ready()`；其余 LVGL API 均在本 Task。

## 资源与约束

- 不创建第二个 LVGL 执行上下文；
- 不手改 `GUI/` 或 `SquareLineProject/`；
- 初始化失败没有部分回滚；
- 第一刀窗口固定从 `Index = 0` 要一窗；滑窗与游标未做；
- 不裁路径、不按文件名切开；标题/歌手等 `load` 调解析器，见 `docs/catalog_architecture.md` 第 5 节。

## 命名

任务入口使用 `gui_task()`；通知枚举使用 `GUI_NOTIFY_*`。
