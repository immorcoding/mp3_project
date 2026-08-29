# Components

本目录保存可复用的协议、器件驱动、状态机和基础核心。需要外部能力的 Component 自己拥有 Ops Interface，并在运行时经 Ops + Context 调用 Adapter；Platform 只负责长期持有并绑定这对对象。

## 公开 Interface

- `audio`、`axp2101`、`ft6x36`、`led`、`log`、`sd`、`soft_i2c`、`st7789`、`w25qxx` 各自的公开类型、状态和函数；
- `w25qxx` 当前实现启动阶段 JEDEC ID 识别；`flash_ftl` 逻辑扇区 Component 仍为预留目录。两者的 Interface 所有权见 [../docs/w25q256_architecture.md](../docs/w25q256_architecture.md)。
- 每个 Module 自己定义的 `*_OpsTypeDef` 或回调类型。

## 编译期依赖

- 标准 C；
- 同层已经公开的 Component Interface；
- 注入的 Ops 与 Context 回调。

## 运行时请求路径

Component 通过自身拥有的 Ops Interface 发起外部请求；具体 Adapter 是该 Interface 的 Implementation，Platform 在装配期绑定 Context。Component 源码不包含 Adapter 头，也不需要知道当前 MCU 或 PCB。

## 事件/ISR 路径

Component 不拥有 HAL ISR 入口。若需要异步后端能力，只定义稳定回调或状态语义，由 Adapter/Platform 在适当上下文转发；不得把任务通知策略写入 Component。

## 禁止依赖

- 不包含 HAL、CubeMX `main.h`、USB Device 或具体 `h*` Handle；
- 不持有 PCB 的引脚、实例和启动策略；
- 不把 FreeRTOS 作为核心逻辑的硬依赖。

## 命名

公开 Interface 使用稳定 Module 前缀；私有 Implementation 使用 `snake_case`。Adapter 的具体身份只存在于 `Adapters`。
