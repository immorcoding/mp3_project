# 本地 Issue 约定

本项目不依赖外部 issue 平台。需求、PRD 和可执行 issue 均以本地 Markdown 文件维护。

- 每项功能使用目录 `.scratch/<feature-slug>/`。
- 需求说明写在 `.scratch/<feature-slug>/PRD.md`。
- 可执行 issue 写在 `.scratch/<feature-slug>/issues/<NN>-<slug>.md`。
- 每个 issue 必须有 `Status:` 行，使用 `docs/agents/triage-labels.md` 中定义的状态。
- 评审意见、进展和结论追加在对应 Markdown 文件中，避免只留在对话里。

`.scratch/` 是工作区资料，不等同于正式项目文档；稳定的架构决策应整理到 `docs/adr/`。
