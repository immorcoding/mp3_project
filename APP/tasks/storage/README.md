# Storage Task

Storage task 是 SD 卡生命周期、插拔消抖、FatFs 挂载/卸载与显式格式化的唯一任务上下文。它把 GPIO EXTI 边沿解释为“需要重新检查”，而不是“已经插卡/拔卡”。

## 公开 Interface

- `storage_task(void *handle)`：由 App 创建；
- `storage_sd_*()`：仅供同一 Task Module 的调度循环调用，不向其他任务公开文件系统访问。

## 调用的 Interface

- `Platform_SD_*`；
- `Filesystem_*`；
- `LogService_Post()`；
- FreeRTOS 任务通知和 FromISR 通知 Interface。

## 资源与约束

- 本任务独占 FatFs 的 `FATFS`/`FIL` 生命周期；
- 卡检测 ISR 仅调用 `vTaskNotifyGiveFromISR()`；消抖、挂载、卸载、格式化和日志均在任务上下文执行；
- 格式化是破坏性操作，只允许本任务并在卡 READY 时执行；
- 不直接调用 `HAL_SD_*`、`BSP_SD_*` 或访问 `hsd1`。

## 命名

本 Module 的内部 Interface 使用 `storage_sd_*`，Task 入口使用 `storage_task()`。
