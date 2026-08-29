# STM32 HAL Adapter

本目录放置把 STM32 HAL、CubeMX 生成的外设 Handle 或 HAL 全局回调入口，转换为 Component 所属 Interface 的实现。具体 `hsd1`、`hi2s2`、GPIO 引脚和板级时序由 Platform 注入，不由本目录选择。

## 公开 Interface

- 各子目录声明的 `*_STM32HALAdapter_Bind()`、`*_Register()`、`*_Unregister()` 等装配或回调分发 Interface；
- 仅供 Platform 长期持有的 Adapter Context 类型。

当前 `st7789_spi/` 将 SPI 阻塞收发、SPI TX DMA、CS/D-C/RESET GPIO、HAL 时基和注册式 SPI 完成/错误回调装配为 ST7789 Device 的 PortOps；它不包含显示初始化表、帧缓冲、LVGL 或 FreeRTOS 通知。

`temp/` 则直接封装 ADC3 内部 Temperature Sensor、VREFINT 与芯片工厂标定数据，供
Platform 取得 MCU 结温。当前它只有一个 STM32H7 后端，不为尚不存在的第二种实现
预先构造 Component Ops。

`ft6x36_i2c/` 则把 HAL I2C 存储器读取、设备地址探测和 TP_RST 启动初始化实现为
FT6X36 Device 的 PortOps；其中 HAL 时基只在调度器启动前使用。它不注册 TP_IRQ，也不包含 LVGL 输入逻辑。

`led_gpio/` 把 LED Device 的逻辑 ON/OFF 映射为当前 GPIO 的输出电平。逻辑 ON 对应的物理
高低电平由 Platform 注入，因此 LED Device 和上层不需要知道本板 LED 的有效极性。

`w25qxx_qspi/` 把 STM32 HAL QSPI 间接模式的同步命令读写、控制和小数据写实现为 W25Qxx Device 的
`W25Qxx_BusOps`。当前支持启动 JEDEC ID、SR1/SR2、SFDP、W25Q256 `0xEC` Quad I/O 原始读取、`0x34` Quad 页数据传输，以及 QE 的按需安全置位；DMA、自动状态轮询、内存映射与 QSPI
IRQ 回调将在原始 NOR 擦写状态机设计完成后再接入。

## 编译期依赖

- Component 定义的 Ops、状态和事件类型；
- STM32 HAL、CubeMX 生成的外设 Handle 与 HAL 回调约定；
- 同一 Adapter 内部的私有辅助函数。

## 约束

- 不包含或调用 `APP`、`Service`，不拥有任务、队列、通知或产品状态机；
- 不让 Component 反向依赖本目录；由 Component 声明 Ops Interface、由此处实现；
- `irq/` 只负责 HAL 全局回调的唯一所有权与源特定分发，具体业务只由已注册的上层回调在任务中处理。
