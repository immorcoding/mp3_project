# FreeRTOS Project Configuration

本目录保存本项目对 FreeRTOS Kernel 的配置、Hook 与断言处理，是第三方 Kernel 的唯一自维护接缝。

## 可维护文件

- `FreeRTOSConfig.h`：调度、任务诊断、Hook 和中断优先级配置；
- `freertos_hooks.c`：栈溢出、空闲任务等 Hook；
- `freertos_assert.c`：断言与故障停机策略。

## 约束

- 不修改相邻 `Source` 内核实现或 Cortex-M Port；
- Hook 不承载 APP、文件系统或硬件业务策略；
- 调整 `configMAX_SYSCALL_INTERRUPT_PRIORITY`、栈检查或任务统计宏时，同步更新任务/ISR README 与验证步骤。

## 任务通知数组长度

`configTASK_NOTIFICATION_ARRAY_ENTRIES` 是每个 TCB 通知数组的槽位数，不是全系统事件编号。
当前为 3，覆盖 Storage Task 实际占用的槽数量。具体槽位命名由各 APP Task 的枚举持有：
Storage Task 见 `APP/tasks/storage/storage_task.h`，GUI Task 见 `APP/tasks/gui/gui_task.h`。
不同任务可以复用同一索引值。
