# Platform Power

本 Module 将 AXP2101 器件和本板 I2C/GPIO 装配为 Audio、LCD 等供电轨的产品语义，并保存启动诊断。

## 公开 Interface

- `Platform_Power_Init()`；
- `Platform_Power_SetAudio()`、`Platform_Power_SetLCD()`；
- `Platform_Power_GetDiagnostics()`。

## 调用的 Interface

- `AXP2101_*`、`SoftI2C_*`；
- 相应 Adapter Bind Interface；
- CubeMX 的软 I2C GPIO 定义。

## 约束

- 电压、上电次序和电源轨映射是 PCB 策略；改动必须同时核对原理图、数据手册和实测；
- 不向上公开 AXP2101 寄存器或 I2C Context；
- 不在 ISR 或任务中隐式重做启动配置。

## 命名

公开能力使用 `Platform_Power_*`；私有 Implementation 使用 `platform_power_*`。
