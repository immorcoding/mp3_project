# Adapters

Adapter 将 STM32 HAL、USB Device 或其他具体后端转换为 Component 所拥有的 Ops Interface。它可以了解厂商类型，但不选择本 PCB 的实例、引脚、供电轨或启动策略；这些由 Platform 装配。

## 公开 Interface

- 各目录的 `*_Bind()`、`*_Register()`、`*_Unregister()` 等后端装配或回调注册 Interface；
- Adapter Context 类型，仅供 Platform 在装配时创建和长期持有；
- `irq/` 中按硬件源划分的 GPIO EXTI、SDMMC 回调分发 Interface。

## 调用的 Interface

- 对应 Component 所声明的 Ops 和状态类型；
- STM32 HAL、USB Device、CMSIS 等厂商 Interface；
- 同一 Adapter 目录内的私有辅助实现。

## 禁止依赖与约束

- 不包含或调用 `APP`、`Service`；不拥有任务策略、文件系统或产品业务状态机；
- 不在 Adapter 内硬编码 `hsd1`、`hi2s2`、GPIO 或 PCB 极性；这些对象由 Platform Context 注入；
- 不让 Component 反向包含 Adapter 头文件；Ops Interface 必须由消费它的 Component 定义；
- `irq/` 只统一 HAL 全局入口的所有权，不把不同外设压成无类型的通用中断 API。

公开装配 Interface 使用 `<Module>_<Target>Adapter_*` 的 Pascal 分段命名，文件内私有 Implementation 使用 `snake_case`。
