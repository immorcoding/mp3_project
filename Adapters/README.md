# Adapters

Adapters 是满足既有 Interface 的具体实现，按后端性质分为三类：`stm32_hal/` 放置 STM32 HAL、CubeMX 与 USB Device 相关实现；`cortex/` 放置 CMSIS Cortex-M 架构能力；`bridge/` 放置两个 Component Interface 之间的转换。它们都不选择本 PCB 的实例、引脚、供电轨或启动策略；这些由 Platform 装配。

## 公开 Interface

- 各目录的 `*_Bind()`、`*_Register()`、`*_Unregister()` 等后端装配或回调注册 Interface；
- Adapter Context 类型，仅供 Platform 在装配时创建和长期持有；
- `stm32_hal/irq/` 中按硬件源划分的 GPIO EXTI、SDMMC 回调分发 Interface。
- `stm32_hal/temp/` 中读取 STM32H7 内部温度传感器的结温采样 Interface；
- `bridge/` 中实现目标 Component Ops、并调用源 Component 公开 Interface 的转换 Module。

## 编译期依赖

- 对应 Component 所声明的 Ops 和状态类型；
- 所属类别允许的 STM32 HAL、USB Device 或 CMSIS Interface；
- 同一 Adapter 目录内的私有辅助实现。

Adapter 的 `.c` 包含目标 Component 头以实现其 Ops，是依赖倒置的正常形式；`*_Bind()` 和 Context 类型是 Platform 用于装配的 Adapter Interface。

## 运行时请求路径

Adapter 只在已绑定的 Component Ops 被调用时，把稳定请求翻译为 HAL、CMSIS、USB Device 或另一个 Component 的调用；它不决定何时发起产品请求。

## 事件/ISR 路径

IRQ Adapter 可以独占 HAL 全局回调入口，按具体硬件源将轻量事件发布给调用者持有的回调节点。Adapter 不认识 Service Task，不直接调用产品业务或 FreeRTOS 通知。

## 禁止依赖与约束

- 不包含或调用 `APP`、`Service`；不拥有任务策略、文件系统或产品业务状态机；
- 不在 Adapter 内硬编码 `hsd1`、`hi2s2`、GPIO 或 PCB 极性；这些对象由 Platform Context 注入；
- 不让 Component 反向包含 Adapter 头文件；Ops Interface 必须由消费它的 Component 定义；
- `stm32_hal/irq/` 只统一 HAL 全局入口的所有权，不把不同外设压成无类型的通用中断 API。
- `bridge/` 不得为了复用而反向吸收 Platform 的实例、板级策略或 HAL 类型；它只转换两个已有 Component Interface 的语义。

公开装配 Interface 使用 `<Module>_<Target>Adapter_*` 的 Pascal 分段命名，文件内私有 Implementation 使用 `snake_case`。
