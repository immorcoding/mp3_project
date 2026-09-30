# Git

远端、分支与提交格式。

## Rules

- **GIT-1** · provisional · 唯一远端是 GitHub `origin`（`immorcoding/mp3_project`）；不再使用 Gitee。_Why:_ 国内网络与 Gitee 体验不再是约束，tracker 也在 GitHub。
- **GIT-2** · provisional · 提交标题用 Conventional Commits：`type(scope): 中文描述`。type ∈ `feat|fix|refactor|perf|test|docs|build|ci|chore|revert`；scope 取模块名（如 `gui`、`fs`、`pmic`、`harness`），可省；破坏公开 Interface 加 `!`；正文可选。由 `.githooks/commit-msg` 校验。_Source:_ [conventionalcommits.org 1.0.0](https://www.conventionalcommits.org/zh-hans/v1.0.0/)
- **GIT-3** · exploring · 小改动直接进 `main`；按 spec 实现的工作开分支、提 draft PR，PR 关闭对应 issue。
- **GIT-4** · settled · 不用 `--no-verify`，不强推 `main`；Agent Hook 拦截。_Why:_ Hook 是 FAST/CHANGED 的唯一自动闸门。

## Rejected

- `YYYY/M/D HH:MM 中文` 时间前缀标题：与 Git 自带时间戳重复，且无类型信息。（v0.5.0 前的格式）
