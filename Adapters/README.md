# Adapters

Adapter 将 STM32 HAL、USB Device 或其他具体后端转换为 Component 所拥有的 Ops Interface。它可以了解厂商类型，但不选择本 PCB 的实例、引脚、供电轨或启动策略。

## 公开 Interface

- 各目录的 `*_Bind()` 或 GPIO EXTI 注册 Interface；
- Adapter Context 类型，仅供 Platform 在装配时创建和长期持有。

## 调用的 Interface

- 对应 Component 的公开 Ops/状态类型；
- HAL、CubeMX、USB Device 和明确的 FreeRTOS FromISR Interface。

## 禁止依赖

- 不包含 `APP` 或 `Service`；
- 不保存 `hi2s2`、`hsd1` 等具体实例选择；
- 不决定任务优先级、文件系统策略或产品启动顺序。

## 命名

Adapter 的公开装配 Interface 保留 `<Module>_<Target>Adapter_Bind()` 形式，明确替换时需要改变的后端；文件内 Implementation 使用 `snake_case`。
