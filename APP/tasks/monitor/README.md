# Monitor Task

Monitor task 记录各任务栈高水位线，并翻转诊断 LED。它不参与播放器业务，也不拥有其他任务的栈或运行状态。

## 公开 Interface

- `monitor_task(void *handle)`：仅由 `APP/tasks/app_tasks.c` 创建。

- `monitor_warn.h`：未登记任务告警的去重记录（纯 C，主机测试 `Tests/monitor_warn`）。

- `monitor_stats.h`：5 s 批次的 CPU 占比与 `Health` 行的计算和成行（纯 C，主机测试 `Tests/monitor_warn` 的 `monitor_stats`）。`monitor_cpu_format()` 由两次快照的运行计数差值格式化一行，`monitor_cpu_baseline_store()` 存下一窗口的基准，`monitor_health_format()` 格式化欠载与日志丢失。

## 编译期依赖

- FreeRTOS `uxTaskGetSystemState()`、堆分配与延时 Interface；
- `Service_Log_Post()`；
- `Platform_Temp_Read()`；
- `playback_task_get_underrun_total()`（播放任务的只读查询，欠载累计）与 `Service_Log_GetDropStats()`（日志丢失累计）；
- `gui_task_get_max_frame_cycles()`（GUI Task 的只读查询）与 `CortexM7CycleCounter_GetFrequencyHz()`；
- `Platform_LED_Toggle(PLATFORM_LED_ID_STATUS)`。

## 运行时请求与事件路径

任务启动时校准一次 ADC3 温度采样通道，随后按周期采样 FreeRTOS 栈诊断和 MCU 结温并投递日志、经 Platform 翻转诊断 LED；当前没有硬件事件订阅或 ISR 入口。

## 资源与约束

- HighWaterMark 表示历史最小剩余栈，不是实时栈使用量；
- 快照内存由本任务申请并在同一次采集后释放；
- 未登记任务不计算占用率，避免除零；新增任务必须在 `monitor_get_task_stack_words()` 登记栈深，否则它的栈不会出现占用率日志，Monitor 会为该任务名投递一条 `WARN` 日志 `Task not registered in monitor: <name>`（每个任务名只告警一次；去重记录容量为 `MONITOR_WARNED_TASK_MAX`，写满后新名字不再告警，所以看到该 WARN 就要补登记，没看到不代表全部已登记）（#81 板测发现 Playback Task 曾因此缺失）；
- 每次快照后追加一行堆现有与历史最小余量（`xPortGetFreeHeapSize` 与 `xPortGetMinimumEverFreeHeapSize`），历史最小值含快照自身的临时占用；
- 每次快照后输出一行 GUI 最长一帧（WF-12），格式 `GUI max frame: <C> cycles = <U>.<d> us @ <F> MHz`：`C` 为启动以来 `Service_GUI_Process()` 单圈最大核心周期数，`U.d` 为 `C / F` 换算的微秒（一位小数，`F` 为核心频率 MHz）；尚无样本时不输出。该值含渲染与被更高优先级任务抢占的时间，是上界；单调不降，读取后不清零。Monitor 只读，写者是 GUI Task，GUI 帧路径不记日志（DIAG-3）；
- **5 s 一批的行数与节拍（DIAG-3、DIAG-6、#204）**：每 5 s（`MONITOR_TASK_PERIOD_MS` × `MONITOR_SNAPSHOT_INTERVAL`）连续投递一批，当前 12 行：每个任务一行栈占用（7 个任务：`IDLE`、`Tmr Svc`、`Log Task`、`Storage Task`、`Playback Task`、`Monitor Task`、`GUI Task`）、`Heap`、结温、`GUI max frame`、`CPU`、`Health`。一批不分散、不让出，在微秒级投完。批次本身的上界是 `MONITOR_CPU_TASK_MAX`（16）个任务加 5 行固定行 = 21 行，`monitor_task.c` 的 `_Static_assert` 保证它不超过 Service Log 的 30 块消息池；12 行的实际批次留下 18 块给同一时刻到来的其他日志。Log Task 每 5 ms 转交一条（见 [Service Log](../../../Service/log/README.md)），所以一批约 60 ms 排空，其间其他任务（切歌、扫描、曲名）合计要少于 18 条才不会挤掉。拿不到块的投递被计数，计数在 `Health` 行里，所以丢失本身不会静默。
- **`CPU` 行**：`CPU win=<ms>ms <键>=<占比>…`，占比 = 该任务在窗口内的运行计数差值 / 总计数差值，单位 %，一位小数，向下取整；窗口是两批之间的节拍差（首批从启动算起）。键是任务名首词小写后的前四个字符，按字典序排列：`gui`、`idle`、`log`、`moni`、`play`、`stor`、`tmr`。`idle` 即空闲占比（R1.1），所有键之和接近 100 %（取整会少零点几）。运行计数来自 `configGENERATE_RUN_TIME_STATS` 的 TIM5 计数器，32 位回绕时差值仍对；这个占比不是 LVGL 浮层上的 CPU%。总计数没有前进时输出 `CPU unavailable`。
- **`Health` 行**：`Health underrun=<累计>(+<增量>) log_svc=<累计>(+<增量>) log_core=<累计>(+<增量>)`。`underrun` 是开机以来的播放欠载次数（播放任务状态机累加）；`log_svc` 是 `Service_Log_Post()` 因消息池满、队列转移失败或文本过长而丢弃的条数；`log_core` 是日志核心（`Components/log`）队列溢出或发送错误的丢弃数（`LOG_StatsTypeDef.DroppedCount`），由 Log Task 刷新到 Service Log 的副本里，Monitor 不碰日志核心（DIAG-1）。括号内是相对上一批的增量。R1.3 要这三项为 0。USB 主机不读日志时 `log_svc` 与 `log_core` 会涨，这是丢失的真实原因，不是 Monitor 的问题；受影响时段的其他日志行不能当作完整证据。
- 监控频率必须受限，避免诊断日志自身成为负载来源。
- MCU 结温用于诊断，不代表设备周围环境温度；连续采样失败只记录一次错误，成功后解除抑制。
- `monitor_task_config.h` 保存采样基础周期、快照间隔、本地日志缓冲长度、CPU 占比行的缓冲长度与任务数上限，以及未登记任务告警记录的容量与名字长度；CPU 占比与 `Health` 行的缓冲、基准放静态区（只由本任务访问），不占 1 KiB 的任务栈；这些参数只控制本 Task 的诊断负载，不属于 FreeRTOS 或其他 Task 的 Interface。

## 命名

所有本目录符号使用 `monitor_*` 与 `snake_case`。
