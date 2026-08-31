# 存储任务

Storage Task 是 SD 热插拔生命周期决策和 FatFs 卷访问的唯一任务上下文。GPIO EXTI 边沿只表示“稍后重新检查”，并不直接表示“已经插卡”或“已经拔卡”。

## 公开 Interface

- `storage_task(void *argument)`：由 APP 创建的任务入口。
- `storage_sd_*()`：本 Task Module 的内部调度 Interface，不是面向其他任务的通用文件访问 Interface。
- `storage_flash_*()`：本 Task 的 Platform Flash 异步操作协调 Interface；它长期持有唯一的 QSPI IRQ 订阅，并只允许当前 Storage Task 启动、等待和收尾 Flash 操作。
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

卡检测 GPIO EXTI 经 Adapter、Platform SD 回调通知本 Task 的索引 0；Filesystem 私有 DMA 执行器在同一 Task 上下文等待索引 1。`storage_flash` 在 Task 启动时长期订阅 Platform Flash 的 QSPI IRQ，并以索引 2 等待 0xEC QSPI/MDMA 读取完成、`0x34/0x21` 自动状态轮询的匹配、错误或中止；任务醒来后才调用 Platform Flash 收尾。

`storage_task()` 的外层 `for (;;)` 只消费索引 0 并执行卡检测消抖，不代表 Storage Task 只会做消抖。一次 `f_read()` / `f_write()` 会在同一个 Task 的嵌套调用栈中进入 Filesystem 的 SD DMA 执行器，等待索引 1、收尾当前分块后再启动下一分块；一次 Flash MDMA 读取则由 `storage_flash` 在同一 Task 的嵌套调用栈中等待索引 2。ISR 只发布事件，绝不复制数据、维护 Cache 或启动下一笔传输。

## 约束

- 索引 `0` 属于本 Module：GPIO EXTI 增加通知计数，任务在 30 ms 静默期结束后调用 `Platform_SD_Process()`。
- 索引 `1` 不由本任务主循环消费。它属于 Filesystem Service 的同步 SDMMC DMA 执行器；执行器在同一个 Storage Task 上下文、于 FatFs 读写期间等待它。
- 索引 `2` 属于 `storage_flash`。它承载一个 QSPI 异步操作的完成、状态匹配、错误或中止；`storage_flash_read_array()`、`storage_flash_read_diagnostic()`、`storage_flash_erase_diagnostic()` 与 `storage_flash_program_diagnostic_page()` 在内部等待该通知并触发 `Platform_Flash_ProcessOperation()` 判定结果。读取通知不能直接视为数据可用，写擦的状态匹配通知也不能直接视为 Device 已恢复 READY。
- APP 不注册 SDMMC 传输回调，不使用 `BSP_SD_*`、不调用 `HAL_SD_*`，也不访问 `hsd1` 或 DMA bounce buffer。
- 插卡只有在 Platform SD 报告 `READY` 后才挂载；拔卡先注销 FatFs 卷。格式化始终是要求卡 `READY` 的显式破坏性请求。
- 读写测试分支在首次成功挂载后顺序写入 64 MiB、`f_sync()`、顺序读取 64 MiB，再进行不计时完整性校验；所有日志使用 `SD: Bench ...`，校验成功后删除 `0:/__sd_rw_bench.bin`，同一上电周期不重复执行。
- `storage_task_config.h` 保存卡检测消抖静默窗口；`storage_sd_benchmark_config.h` 保存仅 APP 诊断使用的测速数据规模。两者均不是 Filesystem Service 的 DMA 参数或对其他 Task 的公开 Interface。
- `storage_sdram_benchmark_config.h` 决定是否在启动阶段执行破坏性的全 SDRAM 诊断与基准；接入 SDRAM 业务数据后必须关闭，或在所有使用者前独占执行。
- `APP/app_config.h` 的 `STORAGE_FLASH_BENCHMARK_ENABLE` 决定是否在启动阶段运行 Flash 基准；`storage_flash_benchmark_config.h` 保存固定读取范围、4 KiB 工作缓冲、单块 MDMA 等待上限及破坏性自检开关。基准依次打印 `Bench poll read` 和 `Bench MDMA read`；两者读取 1 MiB 都不改变 Flash 内容。`STORAGE_FLASH_BENCHMARK_PROGRAM_ENABLE` 开启时，Platform 只允许以首/尾区域语义擦写 ADR-0009 的两个 4 KiB 自检扇区：Storage Task 每页提交 `0x34` 后等待自动状态匹配，随后完成轮询读回逐字节校验，再使用索引 2 执行一次 MDMA 读回逐字节校验；两条路径都通过后仅输出一条自检成功日志，其中包含不计擦除和读回的 8 KiB 逐页编程端到端速率。该入口只可在 Storage Task 启动早期独占执行。

## 命名

Task 内部 Implementation 使用 `storage_*`；任务入口保持 `storage_task()`；跨层调用使用 `Platform_SD_*`、`Service_Filesystem_*` 与 `Service_Log_*`。
