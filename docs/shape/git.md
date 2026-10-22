# Git

远端、分支与提交格式。

Next id: GIT-6

## collaboration

远端、交付分支与提交约束。

### Rules

- **GIT-2** · settled · 提交标题用 Conventional Commits：`type(scope): 中文描述`。type ∈ `feat|fix|refactor|perf|test|docs|build|ci|chore|revert`；scope 取模块名（如 `gui`、`fs`、`pmic`、`harness`），可省；破坏公开 Interface 加 `!`；正文可选。 _Why:_ 提交类型表达变更目的，便于定位行为变化和回退范围。 _Source:_ [conventionalcommits.org 1.0.0](https://www.conventionalcommits.org/zh-hans/v1.0.0/) _Check:_ `.githooks/commit-msg` 校验 `type(scope)!:` 格式（自测在 FAST）；中文描述与 scope 取法靠审阅。
- **GIT-3** · provisional · 小改动直接进 `main`；按 spec 实现或需要上板的工作开分支、提 draft PR，由 PR 关闭对应 issue（`hw:pending` 的 issue 除外，按 WF-3 由用户贴出证据后关闭）。需要用户检查（如上板）的 PR 一直保持 draft：Agent 未获用户明确授权不转 ready、不合并，获授权后可代为执行（授权对所述操作逐次有效）；带板级验收清单的 spec，其集成 PR 保持 draft 直到清单填写完毕（未测项明确列出并保留 `hw:pending`）、再经用户确认或明确授权后合并，板上发现的问题在集成分支上修。_Why:_ 板级验收只有用户能做，转 ready 或合并即宣告完成；#89 先于板测合并，#90 改为先板测再合并，合并前就在分支上修掉了监控漏登记。_Source:_ PR #1、#2、#90
- **GIT-4** · settled · 不用 `--no-verify`；Agent 不强推任何分支，确需覆盖远端时须用户明确授权（强推仍被 Agent Hook 拦截，授权后需先放行 Hook 或由用户执行）。_Why:_ Hook 是 FAST/CHANGED 的唯一自动闸门；强推丢历史且不可逆。_Check:_ Agent Hook `Get-AgentHookShellViolation`（`scripts/hooks/agent_hooks.Tests.ps1` 在 FAST）。
- **GIT-5** · exploring · Agent 的 worktree 放在 `.claude/worktrees/<name>`（已 git-ignored），草稿与日志放会话临时目录（`$CLAUDE_JOB_DIR/tmp`），不在仓库根创建文件。_Why:_ 0.6.0 工作后仓库根杂物需手工清理。

### Rejected

- `YYYY/M/D HH:MM 中文` 时间前缀标题：与 Git 自带时间戳重复，且无类型信息。（v0.5.0 前的格式）
- 单列“唯一远端是 GitHub、不用 Gitee”的规则：`git remote -v` 只有 `origin`，冷启动会话不会另选远端。（was GIT-1）
