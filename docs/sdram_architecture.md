# SDRAM 架构

> 相关 ADR：[ADR-0006：Cortex-M7 D-Cache 范围与 DMA 缓冲所有权](adr/0006-cache-range-ownership.md)、[ADR-0008：SDRAM 早期初始化与 NOLOAD 缓冲区所有权](adr/0008-sdram-early-init-and-noload-ownership.md)

## 硬件与 FMC 配置

当前 Platform SDRAM 同时兼容 `MT48LC16M16A2-6A` 与 `AS4C16M16SA-7TCN`。两者组织均为
`4 Meg × 16 bit × 4 banks`，总容量为 32 MiB，使用 13 行地址位、9 列地址位、4 Bank 与
8192 行/64 ms 刷新要求。

CubeMX 的 FMC SDRAM Bank1 参数为：16-bit、4 Bank、Row 13、Column 9、CAS 3、时钟周期 2、
Read Burst 开启、Read Pipe Delay 1，以及 `2/10/6/9/3/3/3` 的时序字段。该组合按 AS4 -7
较慢时序等级取整，同时满足 MT48 -6A 的要求。

FMC 内核时钟来自 PLL2：`25 MHz / 5 × 104 / 2 = 260 MHz`；`SDClockPeriod = 2`，故
SDRAM 时钟为 130 MHz。初始化刷新计数为 995，即
`floor(130 MHz × 64 ms / 8192) - 20`，满足 64 ms 内 8192 行刷新要求并预留控制器裕量。

## 所有权与初始化

`Core/Src/fmc.c` 是 CubeMX 所有的 FMC 控制器和引脚配置。其 USER CODE 区的
`SDRAM_EarlyInit()` 是一个启动接缝：启动文件在 `SystemInit()` 后、`.data/.bss` 启动循环前调用它。
该函数只使用局部状态，以 D1HCLK 建立约 32 MHz 的临时 SDRAM 配置并发送 JEDEC 命令，保证早期外部
存储段具备可访问性。CLK ENABLE 后使用至少 12 800 条 NOP 等待不少于 200 µs：复位阶段约 64 MHz
时，NOP 本身已覆盖 200 µs，循环开销只会增加等待时间，满足两种器件中 AS4 所需的较长稳定时间。

进入 `main()` 后，CubeMX 的 `MX_FMC_Init()` 切换 FMC 到 PLL2；随后 `Platform/sdram` 借用
`hsdram1`，在 `Platform_Init()` 的最开始按最终 130 MHz 参数执行：CLK ENABLE → 等待 1 ms →
PRECHARGE ALL → 8 次 AUTO REFRESH → LOAD MODE REGISTER → Program Refresh Rate。早期与正式
初始化之间的 SDRAM 内容不作为有效数据，因此 `NOLOAD` 缓冲区必须由其拥有者在正式初始化后主动填充。

链接脚本当前预留 `0xC0000000` 的 `.sdram_framebuffer (NOLOAD)` 段，首尾按 32 字节对齐。它不从
Flash 加载、也不由启动代码清零；这样可避免启动期搬运大帧缓冲，但首次显示或 DMA 前必须完整写入。

## 诊断与测速

Storage Task 在启动时可选调用 `Platform_SDRAM_RunDiagnostic()`；该任务只负责投递日志，测试算法和
FMC/HAL 细节仍属于 Platform。顺序为：

1. 数据线 walking 1 / walking 0；
2. 地址线镜像与短接检查；
3. 全 32 MiB 地址相关图样写入、D-Cache Clean、D-Cache Invalidate 和读回校验；
4. 使用 Cortex-M7 周期计数器 Adapter 统计写入提交和冷读校验吞吐。

写入测速包含 Clean，因此统计的是 CPU 数据提交到外部 SDRAM 的时间；读取前失效 D-Cache，防止
Cache 命中被错误当成 SDRAM 带宽。DWT 运行在 480 MHz Cortex-M7 核心时钟，而不是 130 MHz SDRAM 时钟。

上述 D-Cache 操作统一通过 `Adapters/cortex/cache` 的通用维护 Interface 执行；DWT 周期数和
`SystemCoreClock` 统一通过 `Adapters/cortex/cycle_counter` 读取。Platform SDRAM 不直接调用 CMSIS
的 `SCB_*DCache_by_Addr()`、CoreDebug、DWT 或内核屏障；DMA Adapter 与 SDRAM 诊断只是在不同资源
语义下复用同一份 Clean、Invalidate 和 Clean + Invalidate Implementation。

该诊断会覆写完整 SDRAM，必须在任何外部堆、LVGL 帧缓冲、音频缓存或 DMA 缓冲使用前完成。启用开关
位于 `APP/tasks/storage/storage_sdram_diagnostic_config.h`；后续把业务对象放进 SDRAM 前，应默认关闭。

## 后续使用

诊断通过后，下一阶段应单独审查 MPU 区域属性、Cache 与 DMA 一致性、链接器段位置和每个长期缓冲区的
所有权。不要因为能读写就直接把 FreeRTOS Heap、任务栈和全局 `.bss` 一并迁移到 SDRAM。
