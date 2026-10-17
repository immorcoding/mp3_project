# FreeRTOS Project Configuration

本目录保存本项目对 FreeRTOS Kernel 的配置、Hook 与断言处理，是第三方 Kernel 的唯一自维护接缝。

## 可维护文件

- `FreeRTOSConfig.h`：调度、任务诊断、Hook 和中断优先级配置；
- `freertos_hooks.c`：栈溢出停机 Hook，以及空闲任务和定时器服务任务的静态内存 Hook；
- `freertos_assert.c`：断言与故障停机策略。

## 约束

- 不修改相邻 `Source` 内核实现或 Cortex-M Port；
- Hook 不承载 APP、文件系统或硬件业务策略；
- 调整 `configMAX_SYSCALL_INTERRUPT_PRIORITY`、栈检查或任务统计宏时，同步更新任务/ISR README 与验证步骤。

## 内核任务静态内存

按 [ADR-0019](../../../../docs/adr/0019-freertos-static-task-stack.md) 启用静态分配，保留动态分配，堆大小不变；不定义 `configKERNEL_PROVIDED_STATIC_MEMORY`，由项目提供内存：

- `vApplicationGetIdleTaskMemory()`：独立的 `StaticTask_t` 与 `StackType_t[configMINIMAL_STACK_SIZE]`；
- `vApplicationGetTimerTaskMemory()`：独立的 `StaticTask_t` 与 `StackType_t[configTIMER_TASK_STACK_DEPTH]`，仅在启用软件定时器时编译。

两个 Hook 在调度器启动期间由内核调用，不阻塞、不分配堆；输出栈深度以 `StackType_t` 元素数计。函数内静态对象覆盖内核任务的整个生命周期，经现有链接脚本落入 DTCM `.bss`，由启动代码在调度器启动前清零。它们只由 CPU 使用，不涉及 DMA 或 Cache 维护。现有 APP 任务继续通过 `xTaskCreate()` 动态创建，任务规格与启动日志代码保持不变。

当前 FreeRTOS 内核在启用静态分配时，也会自动将定时器命令队列的控制块与存储区改为静态对象。核算 DTCM 与堆变化时应一并计入；无需修改内核或额外配置宏。

## 任务通知数组长度

`configTASK_NOTIFICATION_ARRAY_ENTRIES` 是每个 TCB 通知数组的槽位数，不是全系统事件编号。
当前为 4，不少于 Storage Task 实际占用的槽数量。具体槽位命名由各 APP Task 的枚举持有：
Storage Task 见 `APP/tasks/storage/storage_task.h`，GUI Task 见 `APP/tasks/gui/gui_task.h`。
不同任务可以复用同一索引值。
