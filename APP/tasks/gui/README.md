# GUI Task

GUI Task 是 LVGL 的唯一执行上下文。它初始化 GUI Service，随后循环：Consume 一次输入、调用各产品分区 `step`、再 `Service_GUI_Process()`。SPI DMA 刷新期间的等待由 GUI Service 的 wait callback 完成。本目录不拥有 SquareLine 生成代码，也不修改 `GUI/`。

`gui_task.c` 不包含 `storage_listbuffer.h` 或 `storage_playback_cursor.h`。音乐播放器在 `music/`：窗口协议、playing 标志与游标步进都在该分区内。Books / Settings 以后各加一个 `step`，不要把它们的 `switch` 写回任务主循环。

## 公开 Interface

- `gui_task(void *handle)`：仅由 `APP/tasks/app_tasks.c` 创建。
- `Gui_NotifyIndexTypeDef`：本任务私有通知槽。`GUI_NOTIFY_LCD_TRANSFER` 由 GUI Service 等待 LCD SPI DMA 完成；槽位编号只在本任务通知数组内有效，与 Storage Task 互不相关。

## 编译期依赖

- `Service_GUI_Init()` / `Service_GUI_Process()` / `Service_GUI_ConsumeInput()`；
- `music/gui_music.h`；
- FreeRTOS Task 入口签名。

## 运行时请求与事件路径

任务启动时调用 `Service_GUI_Init(GUI_NOTIFY_LCD_TRANSFER)`；失败视为致命并进入 `Error_Handler()`。随后 `gui_music_init()`，再循环：把 `input.command` 置 `NONE`，`ConsumeInput`，`gui_music_step`，`Process()`。LCD DMA 最终 ISR 只通知本任务并调用 `lv_disp_flush_ready()`；其余 LVGL API 均在本 Task。

## 资源与约束

- 不创建第二个 LVGL 执行上下文；
- 不手改 `GUI/` 或 `SquareLineProject/`；
- 初始化失败没有部分回滚；
- 不裁路径、不按文件名切开；标题/歌手等 `load` 调解析器，见 `docs/catalog_architecture.md` 第 5 节。

## 命名

任务入口使用 `gui_task()`；通知枚举使用 `GUI_NOTIFY_*`。音乐分区符号使用 `gui_music_*`。
