# AXP2101 Component

AXP2101 Component 实现芯片识别、寄存器协议、供电轨控制和器件诊断，不拥有当前 PCB 的 I2C 引脚或上电策略。

## 公开 Interface

- `AXP2101_Init()`、`AXP2101_ApplyConfiguration()`；
- 各供电轨控制 Interface、Handle、Bus Ops 与归一化错误状态。

## 编译期依赖

- 自身拥有的 AXP2101 Bus Ops 类型。

## 运行时请求与事件路径

Device 经自身拥有的 Bus Ops 请求外部总线；Bridge 或其他 Adapter 实现 Ops，Platform 负责绑定 Context。当前无 AXP2101 ISR 业务路径。

## 约束

- 不包含 SoftI2C、HAL 或 GPIO 头；
- 不新增虚假的“通用 PMIC”抽象；
- 寄存器常量保留在 `axp2101_regs.h`，不能泄漏到上层产品流程。
