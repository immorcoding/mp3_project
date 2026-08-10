# AXP2101 SoftI2C Adapter

本 Adapter 把 SoftI2C 的寄存器读写语义转换为 AXP2101 Component 所需的 Bus Ops。

## 公开 Interface

- `AXP2101_SoftI2CAdapter_Bind()`；
- 对应 Adapter Context 类型。

## 调用的 Interface

- `AXP2101_*` Bus Ops；
- `SoftI2C_*` Interface。

## 约束

- 不认识具体 GPIO、I2C 引脚或 AXP2101 的 PCB 供电策略；
- SoftI2C Handle 由 Platform 长期持有；
- 后端状态要转换为 AXP2101 的归一化 Bus 状态。
