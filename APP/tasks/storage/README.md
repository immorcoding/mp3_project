# 存储任务

Storage Task 是 SD 热插拔生命周期决策和 FatFs 卷访问的唯一任务上下文。GPIO EXTI 边沿只表示“稍后重新检查”，并不直接表示“已经插卡”或“已经拔卡”。

## 公开 Interface

- `storage_task(void *argument)`：由 APP 创建的任务入口。
- `storage_sd_*()`：本 Task Module 的内部调度 Interface，不是面向其他任务的通用文件访问 Interface。
- `storage_sd_benchmark_run()`：仅读写测试分支使用的内部诊断入口；成功挂载后由 `storage_sd_init()` 调用，不向其他任务公开。
- `storage_sdram_diagnostic_run()`：本 Task 启动阶段的内部 SDRAM 硬件诊断入口；不向其他任务公开。

## 编译期依赖

- Platform SD 卡检测生命周期 Interface。
- Filesystem Service 的初始化、挂载、卸载和格式化 Interface。
- Log Service 投递 Interface。
- 仅用于卡检测消抖的原生 FreeRTOS 索引任务通知 Interface。
- Platform SDRAM 的启动诊断 Interface。

## 运行时请求与事件路径

卡检测 GPIO EXTI 经 Adapter、Platform SD 回调通知本 Task 的索引 0；Filesytem 私有 DMA 执行器在同一 Task 上下文等待索引 1。正常存储流程由本 Task 向下调用 Platform SD 与 Filesystem，ISR 不执行 FatFs 或挂载策略。

## 约束

- 索引 `0` 属于本 Module：GPIO EXTI 增加通知计数，任务在 30 ms 静默期结束后调用 `Platform_SD_Process()`。
- 索引 `1` 不由本任务主循环消费。它属于 Filesystem Service 的同步 SDMMC DMA 执行器；执行器在同一个 Storage Task 上下文、于 FatFs 读写期间等待它。
- APP 不注册 SDMMC 传输回调，不使用 `BSP_SD_*`、不调用 `HAL_SD_*`，也不访问 `hsd1` 或 DMA bounce buffer。
- 插卡只有在 Platform SD 报告 `READY` 后才挂载；拔卡先注销 FatFs 卷。格式化始终是要求卡 `READY` 的显式破坏性请求。
- 读写测试分支在首次成功挂载后顺序写入 64 MiB、`f_sync()`、顺序读取 64 MiB，再进行不计时完整性校验；所有日志使用 `SD: Bench ...`，校验成功后删除 `0:/__sd_rw_bench.bin`，同一上电周期不重复执行。
- `storage_task_config.h` 保存卡检测消抖静默窗口；`storage_sd_benchmark_config.h` 保存仅 APP 诊断使用的测速数据规模。两者均不是 Filesystem Service 的 DMA 参数或对其他 Task 的公开 Interface。
- `storage_sdram_diagnostic_config.h` 决定是否在启动阶段执行破坏性的全 SDRAM 测试；接入 SDRAM 业务数据后必须关闭，或在所有使用者前独占执行。

## 命名

Task 内部 Implementation 使用 `storage_*`；任务入口保持 `storage_task()`；跨层调用使用 `Platform_SD_*`、`Service_Filesystem_*` 与 `Service_Log_*`。
