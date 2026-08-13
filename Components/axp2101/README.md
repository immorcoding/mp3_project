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
- `axp2101_config.h` 集中保存默认地址、寄存器地址和 ALDO 使能位掩码等私有固定定义；上层不能直接依赖其中的寄存器语义或绕过 Device Interface。
