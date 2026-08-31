# Storage 基准测试

本目录只放由 Storage Task 在启动早期或 SD/Flash 成功挂载后发起的诊断与吞吐测试。它们不属于 Platform、Component、Filesystem Service 或正式的存储业务路径；是否执行完全由各自编译开关决定。`storage_flash_benchmark` 通过 Cortex-M7 Cycle Counter Adapter 使用 DWT 周期计数，不直接访问 CoreDebug 或 DWT 寄存器；该全局计数器只在启动期串行测试窗口使用。

| Module | 介质与范围 | 数据影响 | 启用方式 |
| --- | --- | --- | --- |
| `storage_sd_benchmark` | 经 FatFs、DiskIO 与 SDMMC 的 64 MiB 端到端文件读写 | 临时创建并在校验后删除 `0:/__sd_rw_bench.bin` | `APP/app_config.h` 的 `STORAGE_SD_BENCHMARK_ENABLE` |
| `storage_sdram_benchmark` | 整片 32 MiB SDRAM 的数据线、地址线、图样与吞吐诊断 | 破坏 SDRAM 全部内容 | `storage_sdram_benchmark_config.h` 的 `STORAGE_SDRAM_BENCHMARK_ENABLE` |
| `storage_flash_benchmark` | W25Q256 `0xEC` 物理数组顺序读取；可选首尾 4 KiB 自检 | 读取不修改内容；自检会擦除并覆写 ADR-0009 两个保留扇区 | `APP/app_config.h` 的 `STORAGE_FLASH_BENCHMARK_ENABLE` |

`storage_flash_benchmark_config.h` 当前从物理地址 `0x00000000` 顺序读取 1 MiB、每次 4 KiB；该地址与分块均满足 W25Q256 `0xEC` 的 4-byte 首地址对齐约束和 MDMA 的 32-byte Cache-line 缓冲约束。基准先输出 `Bench poll read`：它包含每次间接 QSPI 命令、HAL 轮询、CPU 搬运和正常中断的端到端耗时。随后经 `storage_flash` 转交 Filesystem Service，由其长期持有 Platform Flash 回调并等待 Storage Task 通知索引 2 输出 `Bench MDMA read`：每块经 QSPI FIFO 阈值请求交给 MDMA，IRQ 只唤醒任务，Service 再在普通上下文调用 `Platform_Flash_ProcessOperation()` 完成 Cache 失效和状态收尾。可选破坏性自检结束后，基准先用轮询 `0xEC` 读取首个 4 KiB 作为参照，再通过 Platform 开启 `0x90000000` 映射并逐字节比对该参照，最后以 `volatile` 源指针顺序读取同一 1 MiB，输出带确定性 checksum 的 `Bench memory-mapped read`。该交叉比对验证映射协议和物理地址对应关系，`volatile` 避免编译器将测速循环优化为无效读取；成功后故意保持映射开启，供后续 Resource Pack 使用。所有读取吞吐数字均以同一 DWT 周期计数器和 `SystemCoreClock` 换算，避免 FreeRTOS Tick 的毫秒量化；它们都不是理论总线带宽，MDMA 结果仍包含实际任务调度与通知延迟。单块未在 `STORAGE_FLASH_BENCHMARK_MDMA_TIMEOUT_MS` 内完成即停止该轮基准，不会继续提交后续读取。

`STORAGE_FLASH_BENCHMARK_PROGRAM_ENABLE` 控制破坏性自检。开启后 APP 只分配一个位于 `.flash_write_buffer` 的 4 KiB AXI SRAM 缓冲；Platform 只接受“首/尾”区域语义，APP 依次通过 `storage_flash_erase_diagnostic()` 提交 `0x21`，通过 `Platform_Flash_FillDiagnosticBuffer()` 取得私有图样，再通过 `storage_flash_program_diagnostic_page()` 分 16 页提交 `0x34`。每笔写擦均由 `HAL_QSPI_AutoPolling_IT()` 等待 WIP 清零，Status Match IRQ 只唤醒 Storage Task，普通上下文调用 `Platform_Flash_ProcessOperation()` 后才提交下一页。随后以轮询 `0xEC` 读回、逐字节比对地址相关图样；再由 Storage Task 按“首、尾”区域语义各启动一次 MDMA `0xEC` 读回，完成普通上下文 Cache 收尾后由 Platform 比较同一私有图样。故每轮自检只写入一次，但会独立验证轮询和 MDMA 两条接收路径；APP 不保存保留扇区的物理地址或图样。两个扇区分别覆盖最低与最高物理地址，因而同时验证扇区边界、32-bit 地址及完整数据路径。成功时只输出一条 `Self-test passed`：其中的 `write 8 KiB` 速率以 DWT 累计每个 256-byte 页从提交 `0x34` 到 Status Match/普通上下文收尾的端到端周期；擦除、图样生成和两条读回校验均不计入该速率。诊断失败不会自动重试，避免同一上电周期重复磨损自检扇区。


## Flash 文件读写基准

storage_flash_benchmark_run() 保持挂载前的物理读取及可选保留扇区自检；
storage_flash_benchmark_run_file() 在 Storage Task 确认 Flash FAT 挂载成功后调用。
新实现在 storage_flash_file_benchmark.c，APP 只调用 Service 文件接口和日志，
不包含 FatFs、不调用 Platform 或 FTL。APP 决定图样、测试规模、时机和结果，
Service 执行文件操作，FatFs 经现有 USER DiskIO 链路访问 FTL。

需要同时满足 APP/app_config.h 的 STORAGE_FLASH_BENCHMARK_ENABLE 和私有配置的
STORAGE_FLASH_BENCHMARK_FILE_ENABLE。总开关保持当前默认 0，文件子开关默认 1；
不因需要测速而自动格式化。总开关可由独立构建的编译定义覆盖。若只需文件测试而不需要
首尾保留扇区擦写，可把 STORAGE_FLASH_BENCHMARK_PROGRAM_ENABLE 设为 0。

| 私有宏 | 默认值与语义 |
| --- | --- |
| STORAGE_FLASH_BENCHMARK_FILE_NAME | __ftlrw.bin；位于实际 USER 卷根目录 |
| STORAGE_FLASH_BENCHMARK_FILE_TOTAL_BYTES | 1 MiB，须是分块整数倍且小于可用容量 |
| STORAGE_FLASH_BENCHMARK_FILE_CHUNK_BYTES | 4 KiB 静态应用缓冲，不直接交 DMA |
| STORAGE_FLASH_BENCHMARK_FILE_ENABLE | 1，在总开关开启且挂载成功时执行 |

流程：仅新建 → 按文件偏移生成图样并写入 → 文件同步 → 关闭 →
重新只读打开并测速 → 关闭 → 再打开逐字节校验且检查 EOF → 关闭 → 删除。
写速包含图样填充、全部写调用及最后同步；读速不含内容比较，校验另跑一遍。
打开、关闭、删除与日志不计入吞吐。计时用 RTOS tick，避免长写入超过 32 位 DWT
周期计数器的回绕间隔；保留中断、调度和底层 GC 带来的实际耗时。

同名文件存在时拒绝测试，不覆盖、不预先删除。只有本轮成功创建的文件才尝试删除；
正常和读写、同步、校验失败都进入清理路径，检查关闭和删除结果。
持续关闭失败时 Service 保留文件槽，无法删除则输出可能残留的错误日志，不假报成功。
重启或掉电遗留文件不会被自动删除；用户确认后清理专用文件再测试。
一次启动只尝试一次，避免失败后循环磨损。删除只释放文件系统簇，不强制擦除 FTL 物理块。

主机测试复用真实 APP/Service/FatFs/USER/FTL 与既有 Fake NOR，覆盖正常删除、
同名保护、非所有者、短写、写入/同步/读取错误、数据损坏、一次及持续关闭失败、
删除失败与文件句柄契约；正常完成后重挂载确认文件不存在且空闲簇恢复。
任务 tick、日志及部分错误返回为替身，不代表硬件性能。

开启基准会实际链接 CP936 双向转换表（合计 174344 B）。
当前正式链接布局下，开启基准的 Debug/Release 均超过第一 FLASH 区；
默认关闭的固件可构建。保持 CP936 的 Release 布局草案已验证：
仅把一张 87172 B 的 oem2uni 表放入空余 FLASH2，可在现有两区容量内链接。
正式链接脚本尚未改变，待用户确认；Debug 还需要额外的容量/优化安排。
不要直接把主机通过视为已经具备可烧录的开启基准固件。
