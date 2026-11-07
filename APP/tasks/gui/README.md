# GUI Task

GUI Task 是 LVGL 的唯一执行上下文。它初始化 GUI Service，随后遍历应用注册表 `APP/apps/app_table.c`：对每个应用调用一次 `gui_init`，之后每圈 Consume 一次输入、对每个应用调用 `gui_step`、再 `Service_GUI_Process()`，最后按 `Process()` 的返回值 `vTaskDelay`；注册表里的空字段跳过。SPI DMA 刷新期间的等待由 GUI Service 的 wait callback 完成。本目录不创建界面对象；界面由 `Service/gui/view/` 手写。

`gui_task.c` 不包含任何应用头，只包含 `APP/apps/app_table.h`。音乐播放器在 `APP/apps/music/gui/`（[README](../../apps/music/gui/README.md)）：窗口与曲名的最新需求写入、前台结果读取、播放命令与播放快照的归约都在该分区内（GUI 不等文件、不查询卷状态，经 `ui_cell` 的格子与前台任务交接，见 [前台任务](../foreground/README.md)）；GUI 不读写播放游标、不持 playing 真值。Books / Settings 以后在注册表里各加一行，不要把它们的 `switch` 写回任务主循环；接入约定见 [APP/apps](../../apps/README.md)。

## 公开 Interface

- `gui_task(void *handle)`：仅由 `APP/tasks/app_tasks.c` 创建。
- `gui_task_get_max_frame_cycles()`：只读查询，返回启动以来单圈 `Service_GUI_Process()`（含渲染）的最大核心周期数；GUI Task 是唯一写者，Monitor 在周期日志中换算输出（WF-12）。睡眠在测量段之外，不计入。
- `GUI_NotifyIndexTypeDef`：本任务私有通知槽。`GUI_NOTIFY_LCD_TRANSFER` 由 GUI Service 等待 LCD SPI DMA 完成；槽位编号只在本任务通知数组内有效，与其他任务互不相关。

## 编译期依赖

- FreeRTOS `vTaskDelay()`（`INCLUDE_vTaskDelay` 已开）；
- `Service_GUI_Init()` / `Service_GUI_Process()` / `Service_GUI_ConsumeInput()` / `Service_GUI_TransportApply()` / `Service_GUI_ProgressApply()` / `Service_GUI_TimeApply()` / `Service_GUI_TitleApply()` / `Service_GUI_VinylApply()` / `Service_GUI_QueueApply()`（后几项由音乐 GUI 分区调用，不经本任务）；
- `APP/apps/app_table.h`（注册表的 `gui_init`、`gui_step`）；
- FreeRTOS Task 入口签名。

## 运行时请求与事件路径

任务启动时调用 `Service_GUI_Init(GUI_NOTIFY_LCD_TRANSFER)`；失败视为致命并进入 `Error_Handler()`。随后按注册表顺序调用各应用的 `gui_init`，再循环：把 `input.command` 置 `NONE`，`ConsumeInput`，按注册表顺序调用各应用的 `gui_step`，`Process()`，`vTaskDelay(pdMS_TO_TICKS(wait_ms))`。LCD DMA 最终 ISR 只通知本任务并调用 `lv_disp_flush_ready()`；其余 LVGL API 均在本 Task。

GUI 睡眠（GUI-28，[#190](https://github.com/immorcoding/mp3_project/issues/190)）：`wait_ms` 是 `Process()` 的返回值，恒为 1 至 16 ms，等于下一个 LVGL 定时器到期前剩余的时间减去本圈耗时，输入槽里有待取走的命令时只有 1 ms（计算在 Service 内，见 [Service/gui](../../../Service/gui/README.md)）。睡眠让出 CPU 给前台、Log、Monitor 等同级和后台任务、Flash 启动任务、IDLE 等低优先级任务；**GUI 优先级 #218 起是 2**（原来是 1，与前台任务、Log、Monitor 同级，时间片轮转；Storage 降到 1）。用 `vTaskDelay` 而不是带超时的通知等待，所以前台、播放的新结果到达不会提前叫醒 GUI：`gui_step` 最慢每 16 ms 看一次，到期前到达的结果赶得上同一次屏幕刷新；若板测发现需要，只把这一行换成带超时的通知等待，`Process()` 的接口不变。本任务不新增通知槽。

## 资源与约束

- 每圈用 DWT 周期计数器（`app_init()` 在 Resource 加载前已启动，本任务只读不复位）测量 `Service_GUI_Process()`，只保存最大值；`vTaskDelay` 必须在测量段之外，否则最长帧会被睡眠撑大；每帧路径不记日志（DIAG-3）。其他模块若在任务上下文复位计数器（如启动阶段的基准），该圈样本可能失真，板测时需排除。
- 不创建第二个 LVGL 执行上下文；
- 不直接创建或修改 LVGL 对象，经 `Service_GUI_*` 公开 Interface 交互；
- 与前台任务同级（2）：GUI 对 `ui_cell` 只用不等的入口（`try_write`、`read_if_changed`），拿不到锁下一帧再试，所以不会被前台任务的文件调用或持锁拖住；GUI 步里不调用文件接口、不等任何锁（ARC-24、ARC-26）；
- 初始化失败没有部分回滚；
- 不裁路径、不按文件名切开；标题/歌手由前台任务读标签后经格子给出，见 `docs/shape/storage.catalog.md` 第 5 节与 [音乐应用](../../apps/music/README.md)。

## 命名

任务入口使用 `gui_task()`；通知枚举使用 `GUI_NOTIFY_*`。音乐分区符号使用 `gui_music_*`，在 `APP/apps/music/gui/`。
