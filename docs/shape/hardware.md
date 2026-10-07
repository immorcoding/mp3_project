# Hardware

本板供电、外部内存与采样输入的硬件约束。分层和生成器边界沿用 [Architecture](architecture.md) ARC-2/3/5/6/8/10，配置归属沿用 [Code style](code-style.md) STY-3。

Next id: HWD-6

## Pillars

- 硬件事实与产品策略分开，配置变更以器件和板上证据验证。
- 缓冲区可访问、内容有效和设备完成是不同条件。
- 输入缺失可降级，硬件诊断不能破坏运行中的业务数据。

## power

电源启动表与同步总线的使用边界。

### Rules

- **HWD-1** · settled · 电源启动表变更须核对原理图、器件手册、负载允许电压、上电顺序与实测；同一 AXP2101/SoftI2C 实例由调用方串行访问。_Why:_ 当前同步读改写无互斥，寄存器表表达板级硬件策略。_Source:_ [ADR-0003](../adr/0003-pmic-softi2c-bridge.md) _Check:_ code-review 规范轴 对照电源参考与板级证据审阅。

### References

- [硬件依据表](sources/hardware-facts.md)：电源轨与开漏电平依据；操作接口由 [Platform Power](../../Platform/power/README.md) 维护。

## memory

SDRAM 初始化后内容、诊断与运行数据的所有权。

### Rules

- **HWD-2** · settled · NOLOAD 缓冲由拥有者在正式 SDRAM 初始化后、首次 CPU/DMA 使用前完整填充；全容量诊断仅在业务占用前独占运行，新增堆、栈或段先审查启动、MPU 与 Cache/DMA 所有权。_Why:_ NOLOAD 不自动清零，两次初始化之间内容无效，诊断覆写全容量。_Source:_ [ADR-0008](../adr/0008-sdram-early-init-and-noload-ownership.md) _Check:_ code-review 规范轴 检查链接段、首次写入与诊断调用时机。

### References

- [硬件依据表](sources/hardware-facts.md)：器件兼容、刷新和启动等待的依据；初始化接口与诊断见 [Platform SDRAM](../../Platform/sdram/README.md)。

## input

触摸供电、坐标与故障降级。

### Rules

- **HWD-3** · settled · 触摸初始化在 LCD 供电稳定后、调度器启动前完成；Platform 发布原始坐标，可用性失效后停止 I2C 读点，GUI 持续报告释放；方向变换只在 GUI 输入接缝完成。_Why:_ 模组共享 ALDO2，启动复位阻塞，故障重试会阻塞采样并留下按下状态。_Source:_ [Platform Touch](../../Platform/touch/README.md)、[ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md) _Check:_ code-review 规范轴核对启动顺序、故障短路与坐标归属，真机检查输入。

### References

- [Platform Touch](../../Platform/touch/README.md)：采样可用性；[FT6X36](../../Components/ft6x36/README.md)：原始触点协议；[硬件依据表](sources/hardware-facts.md)：模组识别边界。

## temperature

当前低频结温测量的 ADC 配置契约。

### Rules

- **HWD-4** · settled · 结温轮询保持 ADC3 双 Rank（温度、VREFINT）、16 bit、软件单次启动、单转换 EOC 与 Auto Wait；满足传感器稳定/采样时间，DMA 或中断采样需另建资源与完成路径。_Why:_ ADC DR 单槽会被后续 Rank 覆盖，当前通过读取后再转换保证顺序。_Source:_ [Temperature Adapter](../../Adapters/stm32_hal/temp/README.md) _Check:_ code-review 规范轴核对 CubeMX 源配置与采样实现、板级结温证据。

### References

- [硬件依据表](sources/hardware-facts.md)：电压补偿和校准失效依据；[Temperature Adapter](../../Adapters/stm32_hal/temp/README.md)：采样接缝。

## transfer

LCD SPI 事务的完成判据与缓冲归还。

### Rules

- **HWD-5** · settled · LCD 分块 DMA 等待 SPI EOT 后才续块，最后一块 EOT 后才释放 CS、归还绘制缓冲并报告最终完成；DMA TC 不作为 RAMWR 完成。_Why:_ DMA TC 仅说明数据进入 SPI FIFO，过早归还会截断事务或覆盖在飞像素。_Source:_ [ST7789 SPI Adapter](../../Adapters/stm32_hal/st7789_spi/README.md) _Check:_ [代码审阅入口](../../CODING_STANDARDS.md) 的规范轴对照 SPI 回调、CS 和 GUI 缓冲所有权审阅，板级验证最终完成时序。

### References

- [LCD Platform](../../Platform/lcd/README.md)、[GUI Service](../../Service/gui/README.md)：最终事件与 LVGL 归还接缝。
