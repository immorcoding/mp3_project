# 工程 Shape

当前标准按领域阅读：先按 [目录路由](ROUTES.md) 选择模块，再读相关领域，不整本加载所有正文。各 area 第 3 行写明它管什么；模块接口与运行路径查就近 README 和公开头。

领域：[Architecture](architecture.md) · [Code style](code-style.md) · [Storage](storage.md) · [Resources](resources.md) · [GUI](gui.md) · [Hardware](hardware.md) · [Diagnostics](diagnostics.md) · [Harness](harness.md) · [Workflow](workflow.md) · [Git](git.md)。命令与验证状态见 [验证参考](sources/verification.md)，审阅指针见 [CODING_STANDARDS](../../CODING_STANDARDS.md)。

术语按条查根 [GLOSSARY](../../GLOSSARY.md)；历史取舍查 [ADR 索引](../adr/README.md)。事项、进度与验收证据在 GitHub，读取约定见 [tracker](../agents/issue-tracker.md)；领域词典与 ADR 的职责见 [domain](../agents/domain.md)；派 subagent 实施票的常驻步骤见 [implementer-brief](../agents/implementer-brief.md)。

shape 文件格式按 shape-your-project 的 `STANDARD-FORMAT.md`，结构部分由 `scripts/check-docs.ps1` 机械检查。

## 结构与内容审阅

文档变更核对每种材料只在一处维护（归属表见 [domain](../agents/domain.md)，另见 [WF-9](workflow.md#standards-evolution)、[HAR-9](harness.md#entrypoints)）、引用可达、术语足以辨别实际概念；缩短行数不代替内容守恒。每条规则应改变未来决策；sources 保留不可替代的事实或格式依据，不收旧章节改名后的原文，也不把无关约束合为一条。
