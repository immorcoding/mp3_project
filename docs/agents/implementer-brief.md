# 实施者简报（派给 subagent 的常驻指令）

派 subagent 实施票时读；派发提示只需写票号与票专属要求，其余以本页为准。

## 步骤

1. 工作位置：在 `.claude/worktrees/<name>` 的 worktree 内工作（已被 Git 忽略）；不在仓库根改文件、不新建散落文件。完成标准：`git rev-parse --show-toplevel` 指向该 worktree。
2. 临时文件：草稿、日志、输出一律放会话临时目录（`$CLAUDE_JOB_DIR/tmp`，无则系统临时目录）；不写入仓库。完成标准：`git status` 只含预期改动。
3. 改前阅读：[AGENTS.md](../../AGENTS.md)、路径上最近的子 `AGENTS.md`、[ROUTES](../shape/ROUTES.md) 指到的模块 README；涉及 GUI 先读 [gui](../shape/gui.md)。
4. 保护范围：遵守 [AGENTS.md「关键保护」](../../AGENTS.md#关键保护)，不在此复制；超出票授权的改动先问。
5. 验证：按 [verification](../shape/sources/verification.md) 在最后一次提交之后跑 FULL，推荐 `./scripts/capture-verification.ps1 -Mode FULL`（独立保存 metadata.json、stdout.log、stderr.log）。完成标准：顶层 `HARNESS_STATUS=PASS_HOST_ONLY`；GUI 变化另跑场景回归。`2>&1` 合并 stderr 的调用形式已由 `capture-verification.MergedStderr.Tests.ps1` 覆盖，无需改用 `cmd /c`。
6. 提交：按 [git](../shape/git.md) 的 GIT-2 写 `type(scope): 中文描述`，每次提交聚焦单一目的，末尾加 AGENTS/系统提示给定的 `Co-Authored-By` trailer。
7. 禁止：`--no-verify`、强推、设置 `ALLOW_GENERATED_UPDATE`；未被明确要求时不 push、不开或转 PR。
8. shape：不改 `docs/shape/*.md`；Signal 与草稿写入 `docs/shape/inbox/<分支>.md`（HAR-8，分支名中的 `/` 换成 `-`）。
9. 收尾汇报：提交哈希、FULL 结果（`HARNESS_STATUS` 与证据目录）、未决问题、遗留的临时文件位置。
