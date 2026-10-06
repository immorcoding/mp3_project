# ADR-0003：AXP2101、SoftI2C 与 Platform Power 的职责划分

- 状态：已接受（按现有代码追溯记录）
- 日期：2026-08-29
- 相关实现说明：[../pmic_i2c_architecture.md](../shape/hardware.reference.md)

## 背景

当前 PCB 只有 AXP2101，但其实际寄存器、供电轨和启动参数均是芯片与板级特有事实。把它包装成“通用 PMIC”会把 AXP2101 语义隐藏在虚假抽象之后；同时 SoftI2C 既要保持可移植，也不能让 AXP2101 Device 认识 GPIO 或 HAL。

## 决定

1. `Components/axp2101` 保持显式 AXP2101 Device，拥有芯片识别、寄存器协议、Bus Ops、状态与错误语义；不创建通用 PMIC 基类。
2. `Components/soft_i2c` 只拥有开漏时序与 GPIO Ops，不认识 AXP2101、STM32 GPIO 或本板引脚。
3. `Adapters/bridge/axp2101_soft_i2c` 将 SoftI2C 公开 Interface 转换为 AXP2101 Bus Ops；`Adapters/stm32_hal/soft_i2c` 才负责 STM32 HAL GPIO。
4. `Platform/power` 长期持有两个 Component Handle 与 GPIO Context，并拥有本板的 AXP2101 启动配置、Audio/LCD 电源轨映射和启动顺序。

## 后果

- 更换 SoftI2C 为硬件 I2C 时，只替换 Bridge/STM32 后端与 Platform 装配，不修改 AXP2101 Device；
- 更换 PCB 电源轨时，只修改 Platform Power；
- 多任务电源访问将来应由上层串行化，不能把 FreeRTOS 依赖写进两个 Component。
