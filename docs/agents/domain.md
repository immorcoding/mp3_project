# 领域文档读取规则

本项目采用单一领域上下文：根目录 `GLOSSARY.md` 加 `docs/adr/`。按需披露：冷启动只有章程，其余按指针取用。

1. `AGENTS.md` 由工具注入（Claude Code 经 `CLAUDE.md`）；SessionStart Hook 注入分支、脏文件数与待办事项。
2. 任务来自 GitHub issue 时，先 `gh issue view <n> --comments`，只跟随其中给出的指针（spec、ADR、架构文档、术语名）。
3. 动到哪个领域，先读 `docs/shape/` 下对应文件。
4. 跨层、改边界或改公开 Interface 时，读 `docs/shape/architecture.md` 与相关 Module README。
5. 术语到 `GLOSSARY.md` **按条**查，不整本读。
6. 会影响长期边界的新增决定，完成后新增一份 ADR。

`GLOSSARY.md` 只定义项目特有概念，每条一两句话；模块职责、接口和使用约束由模块 README 维护，跨模块设计由技术文档维护，重要取舍由 ADR 记录，目标与阶段状态只在 GitHub spec/ticket。

## ADR 记录范围

对「存在真实替代方案，且选择会长期约束 Module 所有权、Interface 接缝、并发模型、资源生命周期或演进路线」的决定创建 ADR。ADR 记录取舍，不复制调用链、寄存器值、缓存大小、测试输出或 UI 坐标；这些事实仍留在技术文档和 Module README。agent 工作方式、文档入口与工具配置属于 `docs/shape/`，不写 ADR。

已有 Module 的历史决定可按实际代码追溯补录为「已接受（按现有代码追溯记录）」。不为单纯硬件参数、短期实现步骤或尚未形成决定的候选方案强行创建 ADR。
