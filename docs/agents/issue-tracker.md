# 本地 Issue 约定

本项目不依赖外部 issue 平台。需求、PRD 和可执行 issue 均以本地 Markdown 文件维护。

- 每项功能使用目录 `.scratch/<feature-slug>/`。
- 需求说明写在 `.scratch/<feature-slug>/PRD.md`。
- 可执行 issue 写在 `.scratch/<feature-slug>/issues/<NN>-<slug>.md`。
- 每个 issue 必须有 `Status:` 行，使用 `docs/agents/triage-labels.md` 中定义的状态。
- 评审意见、进展和结论追加在对应 Markdown 文件中，避免只留在对话里。

`.scratch/` 是工作区资料，不等同于正式项目文档；稳定的架构决策应整理到 `docs/adr/`。

## 何时写入

会跨多场、且需要验收标准的功能，先建立 `.scratch/<feature-slug>/`。一场内能做完的改动只更新 `CURRENT.md` 本场交接，不必开 scratch。

助手不得在用户明确要求拆 PRD/issue 之前擅自新建 `.scratch/` 目录。`CURRENT.md` 进度只指向活动 slug，不把 issue 正文抄过去。

对话里形成的可执行切片应写入 `issues/`，避免只留在 `CURRENT.md`（交接节约 40 行上限）。
