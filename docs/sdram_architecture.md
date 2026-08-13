# SDRAM 架构

## 硬件与 FMC 配置

当前 PCB 使用 `MT48LC16M16A2-6A`，组织为 `4 Meg × 16 bit × 4 banks`，总容量为 32 MiB。
CubeMX 的 FMC SDRAM Bank1 参数为：16-bit、4 Bank、Row 13、Column 9、CAS 3、时钟周期 2、
Read Burst 开启、Read Pipe Delay 1，以及 `2/10/6/9/3/3/3` 的时序字段。

FMC 内核时钟来自 PLL2，为 270 MHz；`SDClockPeriod = 2`，故 SDRAM 时钟为 135 MHz。
初始化刷新计数使用 1034，对应 64 ms 内 8192 行刷新。

## 所有权与初始化

`Core/Src/fmc.c` 是 CubeMX 所有的 FMC 控制器和引脚配置。`Platform/sdram` 借用其 `hsdram1`，在
`Platform_Init()` 的最开始执行：CLK ENABLE → 等待 1 ms → PRECHARGE ALL → 8 次 AUTO REFRESH →
LOAD MODE REGISTER → Program Refresh Rate。

这不是 `sdram_early_init()`：后者仅适用于启动代码需要在 `main()` 前读写 SDRAM，例如把 `.data`、
`.bss` 或早期堆放入该区域。当前工程尚未将任何链接器段放入 `0xC0000000`，故常规 Platform 初始化
既更安全也能避免启动期重复配置。

## 诊断与测速

Storage Task 在启动时可选调用 `Platform_SDRAM_RunDiagnostic()`；该任务只负责投递日志，测试算法和
FMC/HAL 细节仍属于 Platform。顺序为：

1. 数据线 walking 1 / walking 0；
2. 地址线镜像与短接检查；
3. 全 32 MiB 地址相关图样写入、D-Cache Clean、D-Cache Invalidate 和读回校验；
4. 使用 Cortex-M7 DWT `CYCCNT` 统计写入提交和冷读校验吞吐。

写入测速包含 Clean，因此统计的是 CPU 数据提交到外部 SDRAM 的时间；读取前失效 D-Cache，防止
Cache 命中被错误当成 SDRAM 带宽。DWT 运行在 480 MHz Cortex-M7 核心时钟，而不是 135 MHz SDRAM 时钟。

上述 D-Cache 操作统一通过 `Adapters/cortex/cache` 的通用维护 Interface 执行。Platform SDRAM
不直接调用 CMSIS 的 `SCB_*DCache_by_Addr()`；DMA Adapter 与 SDRAM 诊断只是在不同资源语义下复用
同一份 Clean、Invalidate 和 Clean + Invalidate Implementation。

该诊断会覆写完整 SDRAM，必须在任何外部堆、LVGL 帧缓冲、音频缓存或 DMA 缓冲使用前完成。启用开关
位于 `APP/tasks/storage/storage_sdram_diagnostic_config.h`；后续把业务对象放进 SDRAM 前，应默认关闭。

## 后续使用

诊断通过后，下一阶段应单独审查 MPU 区域属性、Cache 与 DMA 一致性、链接器段位置和每个长期缓冲区的
所有权。不要因为能读写就直接把 FreeRTOS Heap、任务栈和全局 `.bss` 一并迁移到 SDRAM。
