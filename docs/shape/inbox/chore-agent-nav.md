# Inbox: chore/agent-nav

> 以下是 2026-10-19 补 `docs/agents/implementer-brief.md` 并修复 `capture-verification` 自测在 `2>&1` 下误败时记下的待办；规则正文未改。drain 时提给用户。

## Signal

- 2026-10-19 · harness · friction · HAR-5 · 在 Windows PowerShell 5.1 下以 `2>&1` 合并 stderr 调用 FULL 时，`capture-verification.Tests.ps1` 里 `git add` 的 CRLF 警告（stderr）被包成 ErrorRecord，在 `$ErrorActionPreference='Stop'` 下抛错，导致每份派发提示都要写 `cmd /c "... > out 2> err"`。已在自测内局部放宽并加 `capture-verification.MergedStderr.Tests.ps1` 回归（接入 FAST）。
- 2026-10-19 · git · friction · GIT-3 · 0.6.0 工作中 agent 的 worktree 与草稿落在仓库根，事后需手工清理。

## 待办（writer branch drain 时执行）

- `docs/shape/git.md` 加一条（proposed，level exploring）：Agent 的 worktree 放在 `.claude/worktrees/<name>`（已 git-ignored），草稿与日志放会话临时目录（`$CLAUDE_JOB_DIR/tmp`），不在仓库根创建文件。_Why:_ 0.6.0 工作后仓库根杂物需手工清理。_Check:_ 暂无，靠审阅与 `git status`。
- `docs/shape/sources/verification.md` 的 `capture-verification.ps1` 一行补充：合并 stderr（`2>&1`）的调用形式与重定向到文件等价，由 `capture-verification.MergedStderr.Tests.ps1`（FAST）覆盖；不再需要 `cmd /c` 变通。
- 知会：`docs/agents/implementer-brief.md` 已存在，收录派 subagent 实施票的常驻步骤；文档地图或 `harness.md` 若列 agent 文档可补链接。
