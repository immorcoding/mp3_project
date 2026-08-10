# Components

本目录保存可复用的协议、器件驱动、状态机和基础核心。它们通过自身拥有的 Ops Interface 调用外部能力，因此可替换 Adapter 或绑定测试后端。

## 公开 Interface

- `audio`、`axp2101`、`log`、`sd`、`soft_i2c` 各自的公开类型、状态和函数；
- 每个 Module 自己定义的 `*_OpsTypeDef` 或回调类型。

## 调用的 Interface

- 标准 C；
- 同层已经公开的 Component Interface；
- 注入的 Ops 与 Context 回调。

## 禁止依赖

- 不包含 HAL、CubeMX `main.h`、USB Device 或具体 `h*` Handle；
- 不持有 PCB 的引脚、实例和启动策略；
- 不把 FreeRTOS 作为核心逻辑的硬依赖。

## 命名

公开 Interface 使用稳定 Module 前缀；私有 Implementation 使用 `snake_case`。Adapter 的具体身份只存在于 `Adapters`。
