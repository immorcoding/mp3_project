# Platform Power

本 Module 将 AXP2101 器件和本板 I2C/GPIO 装配为 Audio、LCD 等供电轨的产品语义，并保存启动诊断。

## 公开 Interface

- `Platform_Power_Init()`；
- `Platform_Power_SetAudio()`、`Platform_Power_SetLCD()`；
- `Platform_Power_GetDiagnostics()`。

## 编译期依赖与装配

- `AXP2101_*`、`SoftI2C_*`；
- 相应 Adapter Bind Interface；
- CubeMX 的软 I2C GPIO 定义。

## 运行时请求与事件路径

上层经 `Platform_Power_*` 请求产品电源语义；本 Module 调用 AXP2101 Device，Device 经已绑定 Bridge 与 GPIO Adapter 访问总线。当前为同步阻塞路径，PMIC IRQ 尚未接入。

## 约束

- 电压、上电次序和电源轨映射是 PCB 策略；改动必须同时核对原理图、数据手册和实测；
- 不向上公开 AXP2101 寄存器或 I2C Context；
- 不在 ISR 或任务中隐式重做启动配置。

## 私有配置

`platform_power_config.h` 保存本板 SoftI2C 时序以及 AXP2101 `COMMON_CONFIG` 的启动目标位。它代表 PCB 供电策略，不是公开的 PMIC 寄存器 Interface；修改时必须执行原理图、数据手册和实测复核。启动使能与电压预设是独立配置，关闭某路 LDO 不代表应清除它的电压预设。

## 命名

公开能力使用 `Platform_Power_*`；私有 Implementation 使用 `platform_power_*`。
