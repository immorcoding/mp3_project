# 存储任务

Storage Task 是 SD 热插拔生命周期决策和 FatFs 卷访问的唯一任务上下文。GPIO EXTI 边沿只表示“稍后重新检查”，并不直接表示“已经插卡”或“已经拔卡”。

## 公开 Interface

- `storage_task(void *argument)`：由 APP 创建的任务入口。
- `storage_sd_*()`：本 Task Module 的内部调度 Interface，不是面向其他任务的通用文件访问 Interface。
- `storage_sd_benchmark_run()`：仅读写测试分支使用的内部诊断入口；成功挂载后由 `storage_sd_init()` 调用，不向其他任务公开。
- `storage_sdram_benchmark_run()`：本 Task 启动阶段的内部 SDRAM 硬件诊断与基准入口；不向其他任务公开。
- `storage_flash_benchmark_run()`：本 Task 启动阶段的 W25Q256 原始读取及可选双自检扇区破坏性基准入口；不向其他任务公开。

## 编译期依赖

- Platform SD 卡检测生命周期 Interface。
- Filesystem Service 的初始化、挂载、卸载和格式化 Interface。
- Log Service 投递 Interface。
- 仅用于卡检测消抖和 DMA 完成的原生 FreeRTOS 索引任务通知 Interface。
- Platform SDRAM 的启动诊断 Interface。
- Platform Flash 的受限原始数组读取与固定双自检扇区诊断 Interface。

## 运行时请求与事件路径

卡检测 GPIO EXTI 经 Adapter、Platform SD 回调通知本 Task 的索引 0；Filesystem 私有 DMA 执行器在同一 Task 上下文等待索引 1。启用 Flash 基准时，Platform Flash 的 QSPI IRQ 回调临时使用索引 2 唤醒本 Task，随后由基准代码在普通上下文完成 MDMA 读取收尾。正常存储流程由本 Task 向下调用 Platform SD 与 Filesystem，ISR 不执行 FatFs 或挂载策略。

## 约束

- 索引 `0` 属于本 Module：GPIO EXTI 增加通知计数，任务在 30 ms 静默期结束后调用 `Platform_SD_Process()`。
- 索引 `1` 不由本任务主循环消费。它属于 Filesystem Service 的同步 SDMMC DMA 执行器；执行器在同一个 Storage Task 上下文、于 FatFs 读写期间等待它。
- 索引 `2` 仅由 `storage_flash_benchmark` 在启动早期临时订阅 Platform Flash 时使用；它表示一个 QSPI/MDMA 原始读取的完成、错误或中止，任务收到后必须调用 `Platform_Flash_ProcessTransfer()` 判定结果，不能把通知直接视为数据可用。
- APP 不注册 SDMMC 传输回调，不使用 `BSP_SD_*`、不调用 `HAL_SD_*`，也不访问 `hsd1` 或 DMA bounce buffer。
- 插卡只有在 Platform SD 报告 `READY` 后才挂载；拔卡先注销 FatFs 卷。格式化始终是要求卡 `READY` 的显式破坏性请求。
- 读写测试分支在首次成功挂载后顺序写入 64 MiB、`f_sync()`、顺序读取 64 MiB，再进行不计时完整性校验；所有日志使用 `SD: Bench ...`，校验成功后删除 `0:/__sd_rw_bench.bin`，同一上电周期不重复执行。
- `storage_task_config.h` 保存卡检测消抖静默窗口；`storage_sd_benchmark_config.h` 保存仅 APP 诊断使用的测速数据规模。两者均不是 Filesystem Service 的 DMA 参数或对其他 Task 的公开 Interface。
- `storage_sdram_benchmark_config.h` 决定是否在启动阶段执行破坏性的全 SDRAM 诊断与基准；接入 SDRAM 业务数据后必须关闭，或在所有使用者前独占执行。
- `APP/app_config.h` 的 `STORAGE_FLASH_BENCHMARK_ENABLE` 决定是否在启动阶段运行 Flash 基准；`storage_flash_benchmark_config.h` 保存固定读取范围、4 KiB 工作缓冲、单块 MDMA 等待上限及破坏性自检开关。基准依次打印 `Bench poll read` 和 `Bench MDMA read`；两者读取 1 MiB 都不改变 Flash 内容。`STORAGE_FLASH_BENCHMARK_PROGRAM_ENABLE` 开启时，Platform 固定擦写 ADR-0009 的首尾两个 4 KiB 自检扇区，先完成一次轮询读回逐字节校验，再由 Storage Task 使用同一索引 2 通知执行一次 MDMA 读回逐字节校验；两条路径都通过后仅输出一条自检成功日志。该入口只可在 Storage Task 启动早期独占执行。

## 命名

Task 内部 Implementation 使用 `storage_*`；任务入口保持 `storage_task()`；跨层调用使用 `Platform_SD_*`、`Service_Filesystem_*` 与 `Service_Log_*`。
