# GUI Task

GUI Task 是 LVGL 的唯一执行上下文。它初始化 GUI Service，随后循环：Consume 一次输入、调用各产品分区 `step`、再 `Service_GUI_Process()`。SPI DMA 刷新期间的等待由 GUI Service 的 wait callback 完成。本目录不创建界面对象；界面由 `Service/gui/view/` 手写。

`gui_task.c` 不包含 `storage_listbuffer.h`、`storage_namebuffer.h` 或 `playback_contract.h`。音乐播放器在 `music/`：窗口协议、播放命令与播放快照的归约都在该分区内；GUI 不读写播放游标、不持 playing 真值。Books / Settings 以后各加一个 `step`，不要把它们的 `switch` 写回任务主循环。

## 公开 Interface

- `gui_task(void *handle)`：仅由 `APP/tasks/app_tasks.c` 创建。
- `gui_task_get_max_frame_cycles()`：只读查询，返回启动以来单圈 `Service_GUI_Process()`（含渲染）的最大核心周期数；GUI Task 是唯一写者，Monitor 在周期日志中换算输出（WF-12）。
- `GUI_NotifyIndexTypeDef`：本任务私有通知槽。`GUI_NOTIFY_LCD_TRANSFER` 由 GUI Service 等待 LCD SPI DMA 完成；槽位编号只在本任务通知数组内有效，与 Storage Task 互不相关。

## 编译期依赖

- `Service_GUI_Init()` / `Service_GUI_Process()` / `Service_GUI_ConsumeInput()` / `Service_GUI_TransportApply()` / `Service_GUI_ProgressApply()` / `Service_GUI_TimeApply()` / `Service_GUI_TitleApply()` / `Service_GUI_VinylApply()` / `Service_GUI_QueueApply()`（后几项由 `music/` 调用）；
- `music/gui_music.h`；
- FreeRTOS Task 入口签名。

## 运行时请求与事件路径

任务启动时调用 `Service_GUI_Init(GUI_NOTIFY_LCD_TRANSFER)`；失败视为致命并进入 `Error_Handler()`。随后 `gui_music_init()`，再循环：把 `input.command` 置 `NONE`，`ConsumeInput`，`gui_music_step`，`Process()`。LCD DMA 最终 ISR 只通知本任务并调用 `lv_disp_flush_ready()`；其余 LVGL API 均在本 Task。

## 资源与约束

- 每圈用 DWT 周期计数器（`Service_Resource_Init()` 已启动，本任务只读不复位）测量 `Service_GUI_Process()`，只保存最大值；每帧路径不记日志（DIAG-3）。其他模块若在任务上下文复位计数器（如 Storage 基准），该圈样本可能失真，板测时需排除。
- 不创建第二个 LVGL 执行上下文；
- 不直接创建或修改 LVGL 对象，经 `Service_GUI_*` 公开 Interface 交互；
- 初始化失败没有部分回滚；
- 不裁路径、不按文件名切开；标题/歌手等 `load` 调解析器，见 `docs/shape/storage.md#catalog` 第 5 节。

## 命名

任务入口使用 `gui_task()`；通知枚举使用 `GUI_NOTIFY_*`。音乐分区符号使用 `gui_music_*`。
