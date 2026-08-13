# STM32 SoftI2C GPIO Adapter

本 Adapter 将 STM32 HAL GPIO 的开漏写入和电平读取转换为 SoftI2C Component 所需的 GPIO Ops。

## 公开 Interface

- `SoftI2C_STM32HALAdapter_Bind()`；
- `SoftI2C_STM32HALAdapterTypeDef`。

## 编译期依赖

- `SoftI2C_*` GPIO Ops；
- `HAL_GPIO_WritePin()`、`HAL_GPIO_ReadPin()`。

## 运行时请求与事件路径

SoftI2C Component 经已绑定 GPIO Ops 请求引脚操作时，本 Adapter 调用 HAL GPIO；无独立 ISR 或 Task 路径。

## 约束

- Context 由 Platform 注入 SCL/SDA Port 与 Pin；
- 释放总线必须映射为开漏输出置高，不是推挽强推高；
- 不认识 AXP2101 地址、寄存器和任何产品电源策略。
