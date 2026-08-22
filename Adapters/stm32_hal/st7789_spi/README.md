# STM32 HAL ST7789 SPI Adapter

本 Module 把 STM32 HAL 的 SPI 阻塞收发、SPI TX DMA、GPIO CS/D-C/RESET 和 HAL 毫秒时基映射为 `Components/st7789` 定义的 `ST7789_PortOpsTypeDef`。它私有持有一个 SPI DMA 事务状态机：D-Cache Clean、最大 65534 B 的连续分块、HAL SPI 最终 EOT 和错误回调，以及最终结果发布。

## 公开 Interface

- `ST7789_SPI_STM32HALAdapterTypeDef`：Platform 注入 SPI Handle、GPIO、有效电平和超时的 Context；
- `ST7789_SPI_STM32HALAdapter_Bind()`：将该 Context 与 ST7789 Device Handle 成对绑定。

`st7789_spi_stm32_hal_adapter_config.h` 是本 Module 私有配置，当前定义每个 SPI DMA 块的最大字节数。该限制同时满足 HAL 传输计数和 RGB565 两字节像素边界；上层不应自行分块。

## 编译期依赖

- `Components/st7789` 的 `ST7789_PortOpsTypeDef`；
- STM32 HAL 的 `HAL_SPI_Transmit()`、`HAL_SPI_TransmitReceive()`、`HAL_SPI_Transmit_DMA()`、`HAL_SPI_RegisterCallback()`、GPIO 与 Delay Interface；
- `Adapters/cortex/cache` 的 `CortexM7DCache_Clean_Rounded()`，仅用于 Memory-to-SPI DMA 启动前同步 CPU 脏 Cache line；像素缓冲首地址必须按 Cache line 对齐，且 Adapter 必须拥有向后补齐后的范围。

## 运行时请求与事件路径

同步命令、RDDID 和阻塞填充继续直接调用 HAL SPI。异步路径为：ST7789 Device 交付完整像素范围 → Adapter Clean Cache 并启动第一块 DMA → DMA TC 后 H7 HAL 等待 SPI EOT → SPI1 IRQ 中注册的 HAL Tx-complete 回调续发下一块，或在最后一块发布最终结果。CS 的完整 RAMWR 事务生命周期由 ST7789 Device 管理，Adapter 不执行 LVGL 或 FreeRTOS 操作。

当前工程只有一个实际使用 SPI1 异步回调的 LCD Adapter，因此本 Module 直接占有该 `SPI_HandleTypeDef` 的 HAL Register Callback 槽位。出现第二个真实异步 SPI 使用者时，再按 Handle 提取强类型 SPI IRQ 分发 Module；不得提前使用无类型全局回调表。

## 禁止依赖

不得自行引用 `hspi1`、LCD 引脚宏、Platform Power、Service_Log、LVGL 或 FreeRTOS Task；这些具体装配、任务通知与产品流程分别由 Platform LCD 和 APP Task 持有。

## 命名

跨 Module Interface 使用 `ST7789_SPI_STM32HALAdapter_*`；文件内私有 Implementation 使用 `st7789_spi_stm32_hal_*`。
