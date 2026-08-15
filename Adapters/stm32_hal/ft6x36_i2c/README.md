# STM32 HAL FT6X36 I2C Adapter

本 Module 把 STM32 HAL 的 I2C 从机探测、8-bit I2C 存储器读取、复位 GPIO 写入和 HAL 毫秒延时，实现为 `Components/ft6x36` 定义的 PortOps。它在内部完成 HAL 地址的左移转换，因此上层与 Device 始终使用 7-bit I2C 地址。

## 公开 Interface

- `FT6X36_I2C_STM32HALAdapterTypeDef`：Platform 长期持有的 I2C Handle、RESET GPIO、有效电平、探测次数和超时 Context；
- `FT6X36_I2C_STM32HALAdapter_Bind()`：把该 Context 与 FT6X36 Device Handle 成对绑定。

## 编译期依赖

- `Components/ft6x36` 的 PortOps、状态和 Handle 类型；
- STM32 HAL 的 I2C、GPIO 与时基 Interface。

## 运行时请求与事件路径

FT6X36 Device 经已绑定 PortOps 请求复位、延时、I2C 探测或寄存器读取；本 Adapter 再调用 HAL。当前为阻塞同步路径，不注册 `TP_IRQ`、不包含 EXTI 回调，也不进入 LVGL。

## 禁止依赖与约束

- 不硬编码 `hi2c2`、TP_RST、I2C 地址或有效极性；这些对象全部由 Platform Touch 注入；
- 不包含或调用 APP、Service、LogService、LVGL；
- 不在 HAL I2C 中断或 GPIO EXTI 中读取触摸寄存器。

## 命名

跨 Module Interface 使用 `FT6X36_I2C_STM32HALAdapter_*`；文件内私有 Implementation 使用 `ft6x36_i2c_stm32_hal_*`。
