# Code style

自维护 C 的命名、注释和配置；适用 APP/Service/Platform/Components/Adapters 与项目维护的中间件接缝，厂商标识符和移植配置保持原风格。

Next id: STY-10

## naming

文件与公开/私有标识符。

### Rules

- **STY-1** · settled · 文件/目录与私有跨文件接口用 snake_case，文件内私有函数/对象用 static snake_case；公开接口用 Module 前缀的 Pascal 分段：`Service_<Capability>_<Verb>`、`Platform_<Capability>_<Verb>`、`<Module>_<Verb>`、`<Module>_<Target>Adapter_<Verb>`。保留 Service 层名、稳定模块拼写、区分模块的 LOG/Service_Log、Adapter 后端及资源/并发语义；只删重复层名。Task 文件/入口为 `<responsibility>_task`，任务显示名可读；APP 启动和私有任务入口保持 snake_case，不向其他层发布。 _Why:_ 名字要能判断公开能力、私有实现及适配后端。 _Check:_ `CODING_STANDARDS.md` 的命名审阅入口。
- **STY-8** · settled · 类型用已发布 Module 前缀加 `TypeDef`；回调用 `*_Callback_t` 或既有 `*Func`，Ops 用 `*_OpsTypeDef`；宏、枚举、编译开关用大写 Module 前缀。新增层级不用模糊的 Board/BSP/*_port；既有 Component PortOps 是设备端口契约，厂商/CubeMX 文件名与符号保持不变。 _Why:_ 类型和宏保留模块归属，避免设备端口契约被泛化层名遮蔽。 _Check:_ `CODING_STANDARDS.md` 的命名审阅入口。

## comments

定义处的契约与原因。

### Rules

- **STY-2** · settled · 所有自维护函数定义紧邻完整中文 Doxygen（公开、static、私有跨文件、回调、强弱后端、测试、头内 static inline 均包括），仅声明处不复制。至少 `@brief`；每个实参名用带方向 `@param`，无参不占位；非 void 用 `@return`/`@retval` 说明结果和失败；旧无方向注释维护时补齐。说明实际阻塞性、次序、所有权、线程/ISR、DMA/Cache、超时和失败缓冲生命周期；测试写场景/断言/替身边界。 _Why:_ 调用约束随实现维护，声明与定义双写容易漂移。 _Check:_ 机械项（紧邻 `/** */`、`@brief`、每个形参 `@param`、非 void 有 `@return`/`@retval`、新函数 `@param` 带方向）由 `scripts/check-doxygen.ps1`（FAST，pre-commit/pre-push，只查本次新增或修改的函数）拦截；描述措辞等判断性部分靠 `CODING_STANDARDS.md` 的注释审阅入口。
- **STY-5** · settled · 自维护 C/H 文件写 `@file`/`@brief`，声明头保留概述、公开类型和关键字段说明；行内注释解释状态机、资源、重试、时序或并发原因，不复述语句，也不替代 Doxygen；代码/目录/行为变化时同步注释，不保留失效层名。 _Why:_ 注释解释跨语句的意图和风险，失效说明会误导下一次修改。 _Check:_ `CODING_STANDARDS.md` 的注释审阅入口。
- **STY-9** · exploring · 注释不用 spec 或 ticket 内部编码（如 B5、K3、A7、D7、E8）作为唯一说明；先用一句话写出含义，再按需链到 spec。_Why:_ #71/#72 集成分支评审指出这些编码对没读过 ticket 的读者不可解，在 `playback_task_config.h`、`app_tasks.c` 等多处出现。_Check:_ 判断类项，靠 `CODING_STANDARDS.md` 的注释审阅入口。

### Signals

- 2026-10-19 · friction · STY-2 · 全仓库存量（`./scripts/check-doxygen.ps1 -Report`）：300 个手写 C/H、1517 个函数定义中 112 个缺文档或缺项（Tools 29、Service 24、Tests 24、Adapters 16、Components 9、APP 8、Platform 2；不含方向）；下次被改动时由检查拦住并顺手补齐，不做集中清欠。
- 2026-10-19 · friction · new · `_Static_assert` 消息用英文还是中文没有规定：#71/#72 两者都有（`playback_task.c`、`gui_music_transport.c` 用英文），STY 只规定注释、文档用中文；用户决定维持现状，不立规则。

## config

配置来源、导出与参数排版。

### Rules

- **STY-3** · settled · 每个 Module 只维护一份 `<module>_config.h`；公开头需要的尺寸/时序/槽位/策略/极性宏及芯片地址/寄存器/位掩码放其中，按所服务 C/H 分组；本模块公开头可包含它再导出，其他模块不直接包含。跨模块契约及上限归拥有方公开类型头，config 引用不复制数字，寄存器细节不泄漏给无关层。 _Why:_ 常量由拥有方维护，复制数字会让契约与实现脱节。 _Check:_ `CODING_STANDARDS.md` 的配置审阅入口。
- **STY-6** · settled · 自维护 config（包括 external_loader）宏用取值后的行末 `/* */`，不写上一行或宏 Doxygen；文件头保留 @file/@brief。用 `/* filename */` 分组，组间/不同用途宏簇间空一行；同文件对齐宏名右缘、取值起点、注释起点，超长值不拖动整列；长表达式可续行，注释放值末行，`#endif /* GUARD */`。厂商/移植配置保持既有格式。 _Why:_ 配置值和用途能逐行对照，便于硬件参数审阅。 _Source:_ `Service/filesystem/flash/filesystem_flash_config.h` _Check:_ `CODING_STANDARDS.md` 的配置审阅入口。

## expression-layout

赋值表达式的换行。

### Rules

- **STY-7** · settled · 赋值表达式避免无意义地在等号后换行。 _Why:_ 保持赋值目标与取值的阅读连续性。 _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 的代码风格审阅入口。
