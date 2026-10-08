# 硬件依据表

供 [Hardware](../hardware.md) 的 Why/Source 使用；本表记录器件事实及其限制，不发布另一套操作规则。公开接口、配置值和流程以源文件及模块 README 为准。

| 事实或测量依据 | 影响 | 证据位置 |
| --- | --- | --- |
| 本板 ALDO1 供音频，ALDO2 供 LCD 和触摸 | 触摸依赖 LCD 电源稳定 | [Power](../../../Platform/power/README.md)、[Touch](../../../Platform/touch/README.md) |
| 软件 I2C RELEASED 是释放开漏，由外部上拉产生高电平 | 不能把它解释为推挽高输出 | [SoftI2C](../../../Components/soft_i2c/README.md)、[GPIO Adapter](../../../Adapters/stm32_hal/soft_i2c/README.md) |
| 两种 SDRAM 为 MT48LC16M16A2-6A、AS4C16M16SA-7TCN；32 MiB、x16、4 bank、13行/9列，8192行/64 ms刷新 | 配置按较慢 AS4 -7 取整兼容；刷新依据为 floor(fSDRAM × 64 ms / 8192) − 20 | [SDRAM](../../../Platform/sdram/README.md)、[配置](../../../Platform/sdram/platform_sdram_config.h) |
| 早期约32 MHz，正式130 MHz；早期CLK ENABLE后至少200 µs，复位约64 MHz时12,800次NOP覆盖该等待 | C运行库前只获得可访问性，两阶段间内容不保真 | [ADR-0008](../../adr/0008-sdram-early-init-and-noload-ownership.md)、[早期接缝](../../../Core/Src/fmc.c) |
| 正式JEDEC包含时钟使能、稳定等待、预充电、8次刷新、模式寄存器及刷新计数；NOLOAD不加载也不清零 | 缓冲首次填充与初始化是两个责任 | [SDRAM实现](../../../Platform/sdram/platform_sdram.c)、[ADR-0008](../../adr/0008-sdram-early-init-and-noload-ownership.md) |
| SDRAM写测速包含Clean、读前Invalidate，DWT计时按核心时钟 | 结果是外存提交与冷读吞吐，不是Cache命中性能 | [诊断实现](../../../Platform/sdram/platform_sdram.c)、[ADR-0006](../../adr/0006-cache-range-ownership.md) |
| FT6X36 Chip ID读取证明寄存器可通信，不保证固定型号值；当前仅取首触点 | 固定ID断言和多指假设均无现有依据 | [FT6X36](../../../Components/ft6x36/README.md)、[Touch](../../../Platform/touch/README.md) |
| ADC仅一个DR，Auto Wait等待读值后才推进下一Rank | 双Rank轮询不会被后一结果覆盖 | [Temperature Adapter](../../../Adapters/stm32_hal/temp/README.md) |
| VREFINT工厂值校正VDDA，raw_temp折算到工厂校准电压后由TS_CAL1/TS_CAL2插值 | 直接插值原始值会将供电变化误读为温变 | [换算实现](../../../Adapters/stm32_hal/temp/temp_stm32_hal_adapter.c) |
| ADC Stop保留offset校准，DeInit、deep-power-down或复位后失效 | 重校准取决于硬件生命周期，不是每次采样 | [Temperature Adapter](../../../Adapters/stm32_hal/temp/README.md)、[HAL校准实现](../../../Adapters/stm32_hal/temp/temp_stm32_hal_adapter.c) |
| DTCM 不经 D-Cache，DMA 不可达；SDMMC 内部 DMA 须访问 AXI SRAM | 放在 DTCM 的栈与静态对象不需要 Cache 维护；DMA 缓冲须放 AXI SRAM 或 SDRAM | [链接脚本段注释](../../../stm32h743zgtx_flash.ld)、[音频 DMA、Cache 与完成事件设计](https://github.com/immorcoding/mp3_project/issues/33) |
