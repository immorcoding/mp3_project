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

