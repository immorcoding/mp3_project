# STM32 HAL ST7789 SPI Adapter

本 Module 把 STM32 HAL 的 SPI 阻塞收发、GPIO CS/D-C/RESET 和 HAL 毫秒时基映射为 `Components/st7789` 定义的 `ST7789_PortOpsTypeDef`。

## 公开 Interface

- `ST7789_SPI_STM32HALAdapterTypeDef`：Platform 注入 SPI Handle、GPIO、有效电平和超时的 Context；
- `ST7789_SPI_STM32HALAdapter_Bind()`：将该 Context 与 ST7789 Device Handle 成对绑定。

## 编译期依赖

- `Components/st7789` 的 `ST7789_PortOpsTypeDef`；
- STM32 HAL 的 `HAL_SPI_Transmit()`、`HAL_SPI_TransmitReceive()`、`HAL_GPIO_WritePin()` 与 `HAL_Delay()`。

## 运行时请求与事件路径

该 Adapter 只在 ST7789 Device 通过已绑定 PortOps 发起请求时调用 HAL；当前仅有阻塞 SPI 读写，不持有 DMA、SPI IRQ 或 LVGL flush 事件。

## 禁止依赖

不得自行引用 `hspi1`、LCD 引脚宏、Platform Power、LogService 或 FreeRTOS Task；这些具体装配与产品流程分别由 Platform LCD 和 APP Task 持有。

## 命名

跨 Module Interface 使用 `ST7789_SPI_STM32HALAdapter_*`；文件内私有 Implementation 使用 `st7789_spi_stm32_hal_*`。
