# SoftI2C Component

SoftI2C Component 实现开漏 I2C 时序、ACK、读写和总线恢复。它只通过最小 GPIO Ops 表达主动拉低、释放和电平读取。

## 公开 Interface

- `SoftI2C_Init()`、设备就绪、主发送/接收、寄存器读写 Interface；
- `SoftI2C_HandleTypeDef`、GPIO Ops、线路和状态类型。

## 调用的 Interface

- Platform 注入的 GPIO Ops 与 Context。

## 约束

- `RELEASED` 表示释放开漏输出，不是强推高电平；
- 不包含 STM32 GPIO 类型，也不认识 AXP2101 寄存器；
- 引脚、延时后端和上拉条件由 Platform/Adapter 保证。
