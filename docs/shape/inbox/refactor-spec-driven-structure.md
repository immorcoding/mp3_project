# Inbox: refactor/spec-driven-structure

- 2026-10-06 · workflow · approved · provisional · 项目特有术语统一在根 GLOSSARY.md 用一两句话定义；模块职责、接口与使用约束归模块 README，跨模块设计归技术文档，长期开发规则归 shape，重要取舍归 ADR，实现目标、验收与进度归 GitHub spec/ticket；优先复用已有承载位置。_Why:_ CONTEXT.md 混入模块名录、实现细节与阶段状态，同一事实多处维护。_Source:_ [文档事实源决定](https://github.com/immorcoding/mp3_project/issues/11#issuecomment-6011748011)。

- 2026-10-06 · workflow · cite · WF-1 · 用户再次确认使用 spec 驱动；本次规划与后续 spec/ticket 继续以 GitHub Issues 为事实源，不重新建立现场文件。
- 2026-10-06 · workflow · cite · WF-2 · 用户确认先明确文档与开发入口重构方案，再形成 spec 和实施 ticket；范围为精简入口、文档职责与旧内容清理，保持现有固件模块边界和运行行为，见[工程文档与开发入口重构地图](https://github.com/immorcoding/mp3_project/issues/10)。
- 2026-10-06 · harness · approved · provisional · 根 AGENTS 保留项目与中文约定、shape 使用方式、关键保护边界摘要、条件阅读指针及验收入口；子 AGENTS 只保留目录独有操作要求，重复正文归对应规范；ROUTES 描述目录职责与必读文档，模块 README 维护职责和接口，CLAUDE 继续仅引用 AGENTS。_Why:_ 避免根、子入口与规范多处重复维护相同内容，同时保留原有保护和验证语义。_Source:_ [开发入口决定](https://github.com/immorcoding/mp3_project/issues/12)。
- 2026-10-06 · harness · cite · HAR-8 · 本轮标准相关信号只写本分支 inbox；领域标准及可能新增的 ROUTES.md 仍按 writer branch 协议处理。
- 2026-10-06 · harness · cite · HAR-5 · 用户确认本次重构沿用 FULL，并补 GUI 场景回归且不改基线；验收还包括入口规模与三类任务导航抽查、迁移清单与引用核对、旧内容分类检查和 independent-verifier，见[重构验收决定](https://github.com/immorcoding/mp3_project/issues/14)。
- 2026-10-06 · workflow · approved · provisional · 现行文档保留有效设计与当前开发流程；过期操作和排障流水由 Git 历史承载，历史 ADR 明确替代关系后保留，不另建 archive 文档堆；资源去留按真实使用依赖判断。_Why:_ GUI 设计文档混排旧 SquareLine 操作与当前实现，容易误导后续开发。_Source:_ [旧内容清理决定](https://github.com/immorcoding/mp3_project/issues/13)。
