# Monitor Task

Monitor task 记录各任务栈高水位线，并翻转诊断 LED。它不参与播放器业务，也不拥有其他任务的栈或运行状态。

## 公开 Interface

- `monitor_task(void *handle)`：仅由 `APP/tasks/app_tasks.c` 创建。

## 编译期依赖

- FreeRTOS `uxTaskGetSystemState()`、堆分配与延时 Interface；
- `LogService_Post()`；
- `Platform_Temp_Read()`；
- `Platform_LED_Toggle(PLATFORM_LED_ID_STATUS)`。

## 运行时请求与事件路径

任务启动时校准一次 ADC3 温度采样通道，随后按周期采样 FreeRTOS 栈诊断和 MCU 结温并投递日志、经 Platform 翻转诊断 LED；当前没有硬件事件订阅或 ISR 入口。

## 资源与约束

- HighWaterMark 表示历史最小剩余栈，不是实时栈使用量；
- 快照内存由本任务申请并在同一次采集后释放；
- 未登记任务不计算占用率，避免除零；
- 监控频率必须受限，避免诊断日志自身成为负载来源。
- MCU 结温用于诊断，不代表设备周围环境温度；连续采样失败只记录一次错误，成功后解除抑制。
- `monitor_task_config.h` 保存采样基础周期、快照间隔和本地日志缓冲长度；这些参数只控制本 Task 的诊断负载，不属于 FreeRTOS 或其他 Task 的 Interface。

## 命名

所有本目录符号使用 `monitor_*` 与 `snake_case`。
