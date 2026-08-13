# 代码命名与注释规范

> 适用范围：`version0.2.3` 的自维护代码。本文是命名和注释的唯一公共规范；目录 README 只补充各 Module 的专用 Interface，不重复本文件。

## 1. 目标与适用范围

规范的目标是让调用者仅通过一个 Module 的公开 Interface 理解其能力，而不必进入其 Implementation 或猜测所属层级。命名应优先表达职责和调用 Seam；不能为了“短”而丢失硬件后端、资源所有权或调用上下文。

本规范约束 `APP`、`Service`、`Platform`、`Components`、`Adapters` 及项目维护的 FreeRTOS/FatFs 接缝。HAL、CubeMX、FreeRTOS、FatFs、USB Device 和厂商库的既有标识符保持原样。

## 2. 文件、目录与 Module

- 目录、C 源文件、私有头文件使用小写 `snake_case`；例如 `monitor_task.c`、`filesystem_service.c`。
- 一个目录应对应一个清晰 Module；其中的 README 说明职责、资源与抽象所有权、公开 Interface、编译期依赖、运行时请求路径、事件/ISR 路径、禁止依赖与生命周期约束。不得把这些内容笼统写成“调用的 Interface”。
- 任务入口文件使用 `<responsibility>_task.c`，入口函数同名；例如 `storage_task()`、`monitor_task()`。
- CubeMX 或第三方目录不修改既有文件名；自维护代码只能使用其明确的 USER CODE 或桥接接缝。

## 3. 标识符命名

| 标识符 | 形式 | 示例 | 说明 |
| --- | --- | --- | --- |
| 文件内私有函数、局部变量、私有静态对象 | `snake_case` | `storage_sd_mount()`、`message_ready_queue` | 必须为 `static`，不得伪装成公开 Interface。 |
| 同一 Module 的私有跨文件 Interface | `snake_case` | `storage_sd_process()` | 仅供同一目录/Module 内部使用；不作为上层依赖。 |
| 跨 Module 的项目公开 Interface | Pascal 分段 + `_` | `Platform_SD_Process()`、`SDCard_ReadBlocks()`、`LogService_Post()` | 前缀是拥有该 Interface 的 Module 名称；每个非缩写单词首字母大写。 |
| Platform 的产品能力 | `Platform_<Capability>_<Verb>` | `Platform_SD_GetInfo()` | 面向上层表达产品能力，不泄漏 HAL Handle、GPIO 或寄存器。 |
| Component 的可复用能力 | `<Module>_<Verb>` | `AXP2101_Init()`、`SoftI2C_MemRead()` | Module 名称按既有稳定拼写保留，例如 `SDCard`、`SoftI2C`。 |
| Adapter 的装配 Interface | `<Module>_<Target>Adapter_<Verb>` | `SDCard_STM32HALAdapter_Bind()` | `STM32HALAdapter` 是有效的 Implementation 身份，不能仅为缩短而删除。 |
| FreeRTOS Task 入口 | `snake_case` | `log_task()` | 满足 `TaskFunction_t` 的 C 函数；任务显示名使用可读字符串。 |
| 类型 | 已发布 Module 前缀 + `TypeDef` | `Platform_SD_InfoTypeDef` | 回调使用 `*_Callback_t` 或既有 `*Func`；操作表使用 `*_OpsTypeDef`。 |
| 宏、枚举值与编译期开关 | 大写 + Module 前缀 | `APP_BOOT_TASK_STACK_WORDS` | 不使用无前缀的通用宏。 |

### 3.1 缩短规则

- 删除只复述目录层次、却不增加语义的信息，例如 `Filesystem_Service_*` 可收敛为 `Filesystem_*`。
- 不删除区分两个真实 Module 的词，例如 `LOG_*`（日志核心）与 `LogService_*`（RTOS 投递/消费 Module）。
- 不删除决定替换方式的 Adapter 身份，例如 `STM32HALAdapter`。
- 公开 Interface 的重命名必须单独列出调用点，完成构建与板上回归；不与无关功能改动混在一起。

## 4. Doxygen 与行内注释

- 每个自维护 `.c`、`.h` 写 `@file` 与 `@brief`；技术文档和代码注释使用中文。
- 公开函数的完整 Doxygen **只写在 `.c` 的定义处**，不在 `.h` 声明重复。应按需说明参数、返回值、阻塞性、调用顺序、所有权、线程安全、ISR 可用性、DMA/Cache 条件和失败含义。
- `.h` 只保留文件概述、公开类型、关键字段及宏的必要说明；不写逐函数 Doxygen。
- 私有函数只在状态机、循环、资源归还、重试、临界区、ISR、DMA 或 Cache 等名称不能充分表达“为什么”的地方添加注释。
- 注释描述原因和约束，不逐字复述 C 语句；目录、命名或行为变化时必须同步更新。

## 5. 审查清单

新增或修改代码时依次检查：

1. 该符号属于哪个 Module，是否确实需要跨 Module 的公开 Interface？
2. 调用者是否只需要理解 Interface，而无需了解 Implementation 的 HAL、任务或硬件细节？
3. 私有符号是否已经保持在本文件/本 Module，维持 Locality？
4. 名称是否保留了必要的 Adapter、资源和并发语义，同时删除了重复层名？
5. Doxygen 是否只位于定义处，且复杂流程是否解释了关键约束？
6. README 是否已经区分功能/抽象所有权、编译期依赖和运行时请求/事件路径？
