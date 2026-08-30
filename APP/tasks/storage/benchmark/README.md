# Storage 基准测试

本目录只放由 Storage Task 在启动早期或 SD 成功挂载后发起的诊断与吞吐测试。它们不属于 Platform、Component、Filesystem Service 或正式的存储业务路径；默认均不执行。

| Module | 介质与范围 | 数据影响 | 启用方式 |
| --- | --- | --- | --- |
| `storage_sd_benchmark` | 经 FatFs、DiskIO 与 SDMMC 的 64 MiB 端到端文件读写 | 临时创建并在校验后删除 `0:/__sd_rw_bench.bin` | `APP/app_config.h` 的 `STORAGE_SD_BENCHMARK_ENABLE` |
| `storage_sdram_benchmark` | 整片 32 MiB SDRAM 的数据线、地址线、图样与吞吐诊断 | 破坏 SDRAM 全部内容 | `storage_sdram_benchmark_config.h` 的 `STORAGE_SDRAM_BENCHMARK_ENABLE` |
| `storage_flash_benchmark` | W25Q256 `0xEC` 物理数组顺序读取；可选首尾 4 KiB 自检 | 读取不修改内容；自检会擦除并覆写 ADR-0009 两个保留扇区 | `APP/app_config.h` 的 `STORAGE_FLASH_BENCHMARK_ENABLE` |

`storage_flash_benchmark_config.h` 当前从物理地址 `0x00000000` 顺序读取 1 MiB、每次 4 KiB。该地址与分块均满足 W25Q256 `0xEC` 的 4-byte 起始地址对齐约束。读速结果包含每次间接 QSPI 命令、HAL 轮询、CPU 搬运和正常中断的端到端耗时，不是理论总线带宽。

`STORAGE_FLASH_BENCHMARK_PROGRAM_ENABLE` 控制破坏性自检。开启后 APP 只分配并传入一个位于 `.flash_write_buffer` 的 4 KiB AXI SRAM 缓冲；`Platform_Flash_RunDiagnostic()` 固定擦除地址 `0x00000000` 与 `0x01FFF000`，对每个扇区分 16 页执行 `0x34` 编程，再以 `0xEC` 读回、逐字节比对地址相关图样。两个扇区分别覆盖最低与最高物理地址，因而同时验证扇区边界、32-bit 地址及完整数据路径。DWT 统计擦除总时长、32 页编程的端到端写速和带校验的读回时长；它们包含命令提交、WIP 轮询、CPU 搬运和同步 QSPI 事务，不代表理论总线带宽。诊断失败也只记录首个阶段和地址，不能自动重试，避免同一上电周期重复磨损自检扇区。
