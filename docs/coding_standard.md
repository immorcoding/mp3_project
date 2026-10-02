# 代码命名与注释规范

> 适用范围：`version0.3.1` 及后续版本的自维护代码。本文是命名、注释与自维护 config 排版的唯一公共规范（提交格式见 `docs/shape/git.md` GIT-2）；目录 README 只补充各 Module 的专用 Interface，不重复本文件。

## 1. 目标与适用范围

规范的目标是让调用者仅通过一个 Module 的公开 Interface 理解其能力，而不必进入其 Implementation 或猜测所属层级。命名应优先表达职责和调用 Seam；不能为了“短”而丢失硬件后端、资源所有权或调用上下文。

本规范约束 `APP`、`Service`、`Platform`、`Components`、`Adapters` 及项目维护的 FreeRTOS/FatFs 接缝。HAL、CubeMX、FreeRTOS、FatFs、USB Device 和厂商库的既有标识符保持原样。

## 2. 文件、目录与 Module

- 目录、C 源文件、私有头文件使用小写 `snake_case`；例如 `monitor_task.c`、`filesystem_service.c`。
- 一个目录应对应一个清晰 Module；其中的 README 说明职责、资源与抽象所有权、公开 Interface、编译期依赖、运行时请求路径、事件/ISR 路径、禁止依赖与生命周期约束。不得把这些内容笼统写成“调用的 Interface”。
- 本目录只维护一份 `<module>_config.h`。打算出现在公开 `.h` 里的宏（条目上限、缓冲区长度、时序、测试规模、板级极性、策略位）以及芯片默认地址、寄存器和位掩码，都写进这份 config，并按所服务的 `.c` / `.h` 文件分组；不为每个源文件再拆一份 config，也不把这类宏散落在公开 `.h` 或 `.c` 顶部。排版见 2.1。
- `<module>_config.h` 仍不是跨 Module Interface：其他 Module 不得直接包含它。本目录的公开 `.h` 若要用这些宏声明数组、槽位或尺寸，则包含本目录 config.h，由公开头再导出。
- 跨 Module 的契约类型与「谁拥有这条上限」留在拥有方的公开类型头（例如 `SERVICE_FILESYSTEM_PATH_MAX_BYTES` 在 `filesystem_types.h`）。其他 Module 的 config 只引用，不复制一份数字。协议寄存在 config 中的细节不得经公开头泄漏给无关层直接依赖。
- 任务入口文件使用 `<responsibility>_task.c`，入口函数同名；例如 `storage_task()`、`monitor_task()`。
- CubeMX 或第三方目录不修改既有文件名；自维护代码只能使用其明确的 USER CODE 或桥接接缝。

### 2.1 `<module>_config.h` 排版

自维护 config（`APP`、`Service`、`Platform`、`Components`、`Adapters`，以及 `Tools/external_loader` 的项目头）按本节排版。FreeRTOS、FatFs、LVGL、CMSIS、CubeMX HAL 等移植/厂商配置保持既有风格，不按本节重排。范例：`Service/filesystem/flash/filesystem_flash_config.h`。

- 每个宏都必须有行末 `/* … */`，写在取值之后；不把说明写在上一行，不用 Doxygen 标宏（禁止 `/** @brief */`、`/**< */`）。文件头的 `@file` / `@brief` 仍保留。
- 按所服务的 `.c` / `.h` 分组：单独一行 `/* filename */`。不同文件的分组之间空一行。
- 同一文件组内，不同用途的宏簇（例如开关、等待、超时）之间空一行。
- 同一文件内对齐三列：宏名右缘、取值起点、行末注释起点。个别取值特别长时，该行注释紧跟取值，不要为对齐它把整列注释拖到右侧。
- 过长表达式可用 `\` 续行；注释放在取值结束的那一行末尾。`#endif` 写成 `#endif /* GUARD */`。

## 3. 标识符命名

| 标识符 | 形式 | 示例 | 说明 |
| --- | --- | --- | --- |
| 文件内私有函数、局部变量、私有静态对象 | `snake_case` | `storage_sd_mount()`、`message_ready_queue` | 必须为 `static`，不得伪装成公开 Interface。 |
| 同一 Module 的私有跨文件 Interface | `snake_case` | `storage_sd_process()` | 仅供同一目录/Module 内部使用；不作为上层依赖。 |
| 跨 Module 的项目公开 Interface | Pascal 分段 + `_` | `Platform_SD_Process()`、`SDCard_ReadBlocks()`、`Service_Log_Post()` | 前缀是拥有该 Interface 的 Module 名称；每个非缩写单词首字母大写。 |
| Platform 的产品能力 | `Platform_<Capability>_<Verb>` | `Platform_SD_GetInfo()` | 面向上层表达产品能力，不泄漏 HAL Handle、GPIO 或寄存器。 |
| Service 的产品流程能力 | `Service_<Capability>_<Verb>` | `Service_Filesystem_MountSD()`、`Service_Log_Post()` | `Service` 前缀明确表示该 Interface 属于产品流程层，不能为缩短而省略。 |
| Component 的可复用能力 | `<Module>_<Verb>` | `AXP2101_Init()`、`SoftI2C_MemRead()` | Module 名称按既有稳定拼写保留，例如 `SDCard`、`SoftI2C`。 |
| Adapter 的装配 Interface | `<Module>_<Target>Adapter_<Verb>` | `SDCard_STM32HALAdapter_Bind()` | `STM32HALAdapter` 是有效的 Implementation 身份，不能仅为缩短而删除。 |
| FreeRTOS Task 入口 | `snake_case` | `log_task()` | 满足 `TaskFunction_t` 的 C 函数；任务显示名使用可读字符串。 |
| APP 的启动接缝与内部任务调度 | `snake_case` | `app_init()`、`app_error()`、`app_task_start()` | `APP` 是一个顶层 Module；`APP/tasks` 只是其私有 Implementation 分区，不对其他层发布 Interface。 |
| 类型 | 已发布 Module 前缀 + `TypeDef` | `Platform_SD_InfoTypeDef` | 回调使用 `*_Callback_t` 或既有 `*Func`；操作表使用 `*_OpsTypeDef`。 |
| 宏、枚举值与编译期开关 | 大写 + Module 前缀 | `APP_BOOT_TASK_STACK_WORDS` | 不使用无前缀的通用宏。 |

### 3.1 缩短规则

- Service 的公开 Interface 必须保留 `Service` 层名前缀，例如 `Service_Filesystem_MountSD()`；该前缀表达产品流程 Seam，不能为了缩短而删除。
- 不删除区分两个真实 Module 的词，例如 `LOG_*`（日志核心）与 `Service_Log_*`（RTOS 投递/消费 Module）。
- 不删除决定替换方式的 Adapter 身份，例如 `STM32HALAdapter`。

## 4. Doxygen 与行内注释

- 每个自维护 `.c`、`.h` 写 `@file` 与 `@brief`；技术文档和代码注释使用中文。
- **每个自维护函数的实现处都必须有完整 Doxygen**，包括公开函数、文件内 `static` 函数、同 Module 私有跨文件函数、回调、强/弱后端实现和测试入口/辅助函数；不能因函数短或名称清楚而省略。
- 函数注释紧邻实际定义，通常位于 `.c`；所有仅声明的位置（包括 `.h` 和 `.c` 前置声明）都不写逐函数 Doxygen，不把正文复制到声明处。头文件若确有 `static inline` 实现，也应在该实现处注释。
- 每个定义至少写 `@brief`；每个形参按实际名称写 `@param[in]`、`@param[out]` 或 `@param[in,out]`（无参数不写占位项）；非 `void` 返回必须用 `@return` 或 `@retval` 解释结果及失败含义。现有未标方向的 `@param` 应确保含义明确，维护时按实际读写方向补齐。
- 按实际语义说明阻塞性、调用顺序、所有权、线程安全、ISR 可用性、DMA/Cache 条件、超时与失败后的缓冲生命周期；测试函数说明场景、断言目标及替身边界，不能只写“执行测试”。
- 仅包含声明的头文件只保留文件概述、公开类型与关键字段说明；宏的说明写在定义处的行末 `/* … */`，不在头文件用 Doxygen 标宏。
- 函数体内的行内注释仍按需添加，解释状态机、循环、资源归还、重试、临界区、ISR、DMA 或 Cache 中的关键原因；行内注释不能替代实现前的 Doxygen。
- 注释描述原因和约束，不逐字复述 C 语句；目录、命名或行为变化时必须同步更新。

## 5. 审查清单

新增或修改代码时依次检查：

1. 该符号属于哪个 Module，是否确实需要跨 Module 的公开 Interface？
2. 调用者是否只需要理解 Interface，而无需了解 Implementation 的 HAL、任务或硬件细节？
3. 私有符号是否已经保持在本文件/本 Module，维持 Locality？
4. 名称是否保留了必要的 Adapter、资源和并发语义，同时删除了重复层名？
5. 每个函数定义是否都有 Doxygen，形参名称和返回语义是否准确，所有声明处是否保持干净，复杂流程是否解释关键约束？
6. 若改了 `<module>_config.h`：是否按文件分组、行末注释、列对齐，且未改移植/厂商配置？
7. README 是否已经区分功能/抽象所有权、编译期依赖和运行时请求/事件路径？
8. 若本次要提交：标题是否符合 `type(scope): 中文描述`（`docs/shape/git.md` GIT-2）？

## 6. Git 提交标题

使用 Conventional Commits 1.0.0，描述用中文：`type(scope): 中文描述`。type 列表、scope 取法与理由只维护在 `docs/shape/git.md` 的 GIT-2，由 `.githooks/commit-msg` 校验。

例：`feat(gui): 唱盘随播放状态旋转`、`fix(harness): 主机测试改读 MP3_HOST_CC`

