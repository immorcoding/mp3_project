# 存储任务

Storage Task 是 SD 热插拔生命周期决策和 FatFs 卷访问的唯一任务上下文。GPIO EXTI 边沿只表示“稍后重新检查”，并不直接表示“已经插卡”或“已经拔卡”。

## 公开 Interface

- `storage_task(void *argument)`：由 APP 创建的任务入口。
- `storage_sd_*()`：本 Task Module 的内部调度 Interface，不是面向其他任务的通用文件访问 Interface。

## 调用的 Interface

- Platform SD 卡检测生命周期 Interface。
- Filesystem Service 的初始化、挂载、卸载和格式化 Interface。
- Log Service 投递 Interface。
- 仅用于卡检测消抖的原生 FreeRTOS 索引任务通知 Interface。

## 约束

- 索引 `0` 属于本 Module：GPIO EXTI 增加通知计数，任务在 30 ms 静默期结束后调用 `Platform_SD_Process()`。
- 索引 `1` 不由本任务主循环消费。它属于 Filesystem Service 的同步 SDMMC DMA 执行器；执行器在同一个 Storage Task 上下文、于 FatFs 读写期间等待它。
- APP 不注册 SDMMC 传输回调，不使用 `BSP_SD_*`、不调用 `HAL_SD_*`，也不访问 `hsd1` 或 DMA bounce buffer。
- 插卡只有在 Platform SD 报告 `READY` 后才挂载；拔卡先注销 FatFs 卷。格式化始终是要求卡 `READY` 的显式破坏性请求。

## 命名

Task 内部 Implementation 使用 `storage_*`；任务入口保持 `storage_task()`；跨层调用使用 `Platform_SD_*`、`Filesystem_*` 与 `LogService_*`。
