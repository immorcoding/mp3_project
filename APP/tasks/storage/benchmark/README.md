# Storage 基准测试

本目录只放由 Storage Task 在启动早期或 SD 成功挂载后发起的诊断与吞吐测试。它们不属于 Platform、Component、Filesystem Service 或正式的存储业务路径；是否执行完全由各自编译开关决定。`storage_flash_benchmark` 通过 Cortex-M7 Cycle Counter Adapter 使用 DWT 周期计数，不直接访问 CoreDebug 或 DWT 寄存器；该全局计数器只在启动期串行测试窗口使用。

| Module | 介质与范围 | 数据影响 | 启用方式 |
| --- | --- | --- | --- |
| `storage_sd_benchmark` | 经 FatFs、DiskIO 与 SDMMC 的 64 MiB 端到端文件读写 | 临时创建并在校验后删除 `0:/__sd_rw_bench.bin` | `APP/app_config.h` 的 `STORAGE_SD_BENCHMARK_ENABLE` |
| `storage_sdram_benchmark` | 整片 32 MiB SDRAM 的数据线、地址线、图样与吞吐诊断 | 破坏 SDRAM 全部内容 | `storage_sdram_benchmark_config.h` 的 `STORAGE_SDRAM_BENCHMARK_ENABLE` |
| `storage_flash_benchmark` | W25Q256 `0xEC` 物理数组顺序读取；可选首尾 4 KiB 自检 | 读取不修改内容；自检会擦除并覆写 ADR-0009 两个保留扇区 | `APP/app_config.h` 的 `STORAGE_FLASH_BENCHMARK_ENABLE` |

`storage_flash_benchmark_config.h` 当前从物理地址 `0x00000000` 顺序读取 1 MiB、每次 4 KiB；该地址与分块均满足 W25Q256 `0xEC` 的 4-byte 首地址对齐约束和 MDMA 的 32-byte Cache-line 缓冲约束。基准先输出 `Bench poll read`：它包含每次间接 QSPI 命令、HAL 轮询、CPU 搬运和正常中断的端到端耗时。随后经 `storage_flash` 使用长期持有的 Platform Flash 回调与 Storage Task 通知索引 2 输出 `Bench MDMA read`：每块经 QSPI FIFO 阈值请求交给 MDMA，IRQ 只唤醒任务，`storage_flash` 再在普通上下文调用 `Platform_Flash_ProcessOperation()` 完成 Cache 失效和状态收尾。三个 Flash 吞吐数字均以同一 DWT 周期计数器和 `SystemCoreClock` 换算，避免 FreeRTOS Tick 的毫秒量化；它们都不是理论总线带宽，MDMA 和页编程结果仍包含实际任务调度与通知延迟。单块未在 `STORAGE_FLASH_BENCHMARK_MDMA_TIMEOUT_MS` 内完成即停止该轮基准，不会继续提交后续读取。

`STORAGE_FLASH_BENCHMARK_PROGRAM_ENABLE` 控制破坏性自检。开启后 APP 只分配一个位于 `.flash_write_buffer` 的 4 KiB AXI SRAM 缓冲；Platform 只接受“首/尾”区域语义，APP 依次通过 `storage_flash_erase_diagnostic()` 提交 `0x21`，通过 `Platform_Flash_FillDiagnosticBuffer()` 取得私有图样，再通过 `storage_flash_program_diagnostic_page()` 分 16 页提交 `0x34`。每笔写擦均由 `HAL_QSPI_AutoPolling_IT()` 等待 WIP 清零，Status Match IRQ 只唤醒 Storage Task，普通上下文调用 `Platform_Flash_ProcessOperation()` 后才提交下一页。随后以轮询 `0xEC` 读回、逐字节比对地址相关图样；再由 Storage Task 按“首、尾”区域语义各启动一次 MDMA `0xEC` 读回，完成普通上下文 Cache 收尾后由 Platform 比较同一私有图样。故每轮自检只写入一次，但会独立验证轮询和 MDMA 两条接收路径；APP 不保存保留扇区的物理地址或图样。两个扇区分别覆盖最低与最高物理地址，因而同时验证扇区边界、32-bit 地址及完整数据路径。成功时只输出一条 `Self-test passed`：其中的 `write 8 KiB` 速率以 DWT 累计每个 256-byte 页从提交 `0x34` 到 Status Match/普通上下文收尾的端到端周期；擦除、图样生成和两条读回校验均不计入该速率。诊断失败不会自动重试，避免同一上电周期重复磨损自检扇区。
