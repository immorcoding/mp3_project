# Git

远端、分支与提交格式。

Next id: GIT-5

## collaboration

远端、交付分支与提交约束。

### Rules

- **GIT-1** · provisional · 唯一远端是 GitHub `origin`（`immorcoding/mp3_project`）；不再使用 Gitee。_Why:_ 国内网络与 Gitee 体验不再是约束，tracker 也在 GitHub。
- **GIT-2** · settled · 提交标题用 Conventional Commits：`type(scope): 中文描述`。type ∈ `feat|fix|refactor|perf|test|docs|build|ci|chore|revert`；scope 取模块名（如 `gui`、`fs`、`pmic`、`harness`），可省；破坏公开 Interface 加 `!`；正文可选。_Source:_ [conventionalcommits.org 1.0.0](https://www.conventionalcommits.org/zh-hans/v1.0.0/) _Check:_ `.githooks/commit-msg` 校验 `type(scope)!:` 格式（自测在 FAST）；中文描述与 scope 取法靠审阅。
- **GIT-3** · provisional · 小改动直接进 `main`；按 spec 实现或需要上板的工作开分支、提 draft PR，由 PR 关闭对应 issue（`hw:pending` 的 issue 除外，按 WF-3 由用户贴出证据后关闭）。需要用户检查（如上板）的 PR 一直保持 draft：Agent 不转 ready、不合并，由用户确认后自行处理。_Why:_ 板级验收只有用户能做，转 ready 或合并即宣告完成。_Source:_ PR #1、#2
- **GIT-4** · settled · 不用 `--no-verify`；Agent 不强推任何分支，确需覆盖远端时由用户本人执行。_Why:_ Hook 是 FAST/CHANGED 的唯一自动闸门；强推丢历史且不可逆。_Check:_ Agent Hook `Get-AgentHookShellViolation`（`scripts/hooks/agent_hooks.Tests.ps1` 在 FAST）。

### Rejected

- `YYYY/M/D HH:MM 中文` 时间前缀标题：与 Git 自带时间戳重复，且无类型信息。（v0.5.0 前的格式）
