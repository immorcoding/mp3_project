# 本地 Issue 约定

本项目不依赖外部 issue 平台。需求、PRD 和可执行 issue 均以本地 Markdown 文件维护。

- 每项功能使用目录 `.scratch/<feature-slug>/`。
- 需求说明写在 `.scratch/<feature-slug>/PRD.md`。
- 本刀合同写在 `.scratch/<feature-slug>/freeze.md`：目标、非目标、接缝与禁止、资源账、必读指针、验收。
- 这台设备此刻的状态写在 `.scratch/<feature-slug>/board.md`：已写入什么、已确认什么、待确认什么。
- 可执行 issue 写在 `.scratch/<feature-slug>/issues/<NN>-<slug>.md`。
- 每个 issue 必须有 `Status:` 行，使用 `docs/agents/triage-labels.md` 中定义的状态。
- 评审意见、进展和结论追加在对应 Markdown 文件中，避免只留在对话里。

`.scratch/` 是工作区资料，不等同于正式项目文档；稳定的架构决策应整理到 `docs/adr/`。

## 三处落点

同一件事的信息按寿命分开放，不要挤进 `CURRENT.md`：

| 寿命 | 载体 | 篇幅 | 内容 |
| --- | --- | --- | --- |
| 跨会话索引 | 根 `CURRENT.md` | 合计约 80 行，单节约 40 行 | 主线、活动 slug、阻塞、分支与脏文件名、verify 与板上各一句、下场第一刀一句 |
| 本刀合同 | `freeze.md` | 合计约 50 行 | 目标、非目标、接缝与禁止、资源账、必读指针、验收 |
| 这台设备此刻 | `board.md` | 合计约 25 行 | 已写入的版本与地址、已确认的板级证据、待确认项 |

稳定格式、分层与术语仍只在 `docs/*_architecture.md`、ADR、Module README 和 `CONTEXT.md`；`freeze.md` 只给指针，不抄正文。`freeze.md` 定范围，不构成改动许可；许可仍只来自用户本条消息，见 `AGENTS.md`。`freeze.md` / `board.md` 超限则拆下一刀或下沉到架构文档，禁止倒灌 `CURRENT.md`。

## 何时写入

会跨多场、且需要验收标准的功能，先建立 `.scratch/<feature-slug>/`。一场内能做完、约束十几行内说得完的改动，只更新 `CURRENT.md` 本场交接，不必开 scratch。一旦出现资源账、地址与几何、跨场必读清单或成串非目标，必须建 slug 并把它们放进 `freeze.md`。

助手不得在用户明确要求拆 PRD/issue 之前擅自新建 `.scratch/` 目录。`CURRENT.md` 进度只指向活动 slug，不把 issue 正文抄过去。

对话里形成的可执行切片应写入 `issues/`，避免只留在 `CURRENT.md`（交接节约 40 行上限）。
