# Monitor Task

Monitor task 记录各任务栈高水位线，并翻转诊断 LED。它不参与播放器业务，也不拥有其他任务的栈或运行状态。

## 公开 Interface

- `monitor_task(void *handle)`：仅由 `APP/tasks/app_tasks.c` 创建。

- `monitor_warn.h`：未登记任务告警的去重记录（纯 C，主机测试 `Tests/monitor_warn`）。

## 编译期依赖

- FreeRTOS `uxTaskGetSystemState()`、堆分配与延时 Interface；
- `Service_Log_Post()`；
- `Platform_Temp_Read()`；
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
- 监控频率必须受限，避免诊断日志自身成为负载来源。
- MCU 结温用于诊断，不代表设备周围环境温度；连续采样失败只记录一次错误，成功后解除抑制。
- `monitor_task_config.h` 保存采样基础周期、快照间隔和本地日志缓冲长度和未登记任务告警记录的容量与名字长度；这些参数只控制本 Task 的诊断负载，不属于 FreeRTOS 或其他 Task 的 Interface。

## 命名

所有本目录符号使用 `monitor_*` 与 `snake_case`。
