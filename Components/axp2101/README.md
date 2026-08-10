# AXP2101 Component

AXP2101 Component 实现芯片识别、寄存器协议、供电轨控制和器件诊断，不拥有当前 PCB 的 I2C 引脚或上电策略。

## 公开 Interface

- `AXP2101_Init()`、`AXP2101_ApplyConfiguration()`；
- 各供电轨控制 Interface、Handle、Bus Ops 与归一化错误状态。

## 调用的 Interface

- Platform 注入的 AXP2101 Bus Ops。

## 约束

- 不包含 SoftI2C、HAL 或 GPIO 头；
- 不新增虚假的“通用 PMIC”抽象；
- 寄存器常量保留在 `axp2101_regs.h`，不能泄漏到上层产品流程。
