# 领域文档读取规则

本项目采用单一领域上下文：根目录 `CONTEXT.md`。冷启动不整本阅读它。不信任对话压缩摘要；硬切后只靠磁盘重建现场。记忆模型与权限不进 `docs/adr/`，见 `AGENTS.md`。

硬切或新开对话后：

1. `AGENTS.md` 由产品注入；立即读完整 `CURRENT.md`；
2. `CURRENT.md` 点了活动 slug，就读 `.scratch/<slug>/` 下存在的 `freeze.md` 与 `board.md`；
3. 只跟随上述文件给出的路径（`.scratch/`、ADR、架构文档、术语名）；
4. 跨层、改边界或改公开 Interface 时，读 `docs/architecture_standard.md` 与相关模块文档；
5. 术语按 `CURRENT.md` 指出的名字、或在上述文档中碰到的名字，到 `CONTEXT.md` **查条**；
6. 对会影响长期边界的新增决定，完成后新增一份 ADR。

`CONTEXT.md` 只放稳定的领域语言、边界和入口信息；具体实现细节放在模块文档或 ADR 中。进度与本场交接只在 `CURRENT.md`，本刀合同与板上事实只在活动 slug 目录，文件约定见 [issue-tracker.md](issue-tracker.md)。

## ADR 记录范围

对“存在真实替代方案，且选择会长期约束 Module 所有权、Interface 接缝、并发模型、资源生命周期或演进路线”的决定创建 ADR。ADR 记录取舍，不复制调用链、寄存器值、缓存大小、测试输出或 UI 坐标；这些事实仍留在技术文档和 Module README。不为 agent 工作方式、文档入口或记忆策略创建 ADR。

已有 Module 的历史决定可按实际代码追溯补录为“已接受（按现有代码追溯记录）”。不为单纯硬件参数、短期实现步骤或尚未形成决定的候选方案强行创建 ADR。
