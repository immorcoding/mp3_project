# 实施者简报（派给 subagent 的常驻指令）

派 subagent 实施票时读；派发提示只需写票号与票专属要求，其余以本页为准。

## 步骤

1. 工作位置：在 `.claude/worktrees/<name>` 的 worktree 内工作（已被 Git 忽略）。完成标准：`git rev-parse --show-toplevel` 指向该 worktree。
2. 临时文件：草稿、日志、输出一律放会话临时目录（`$CLAUDE_JOB_DIR/tmp`，无则系统临时目录）。完成标准：`git status` 只含预期改动。
3. 验证：按 [verification](../shape/sources/verification.md) 在最后一次提交之后跑 `./scripts/capture-verification.ps1 -Mode FULL`；GUI 变化另跑场景回归。完成标准：顶层 `HARNESS_STATUS=PASS_HOST_ONLY`。需要上板的票软件验证通过后挂 `hw:pending`（WF-3）。
4. 交付止于本地提交：push、开 PR 或转 ready 只在派发提示明确要求时做（GIT-3）。
5. 提交：按 GIT-2，末尾加 AGENTS/系统提示给定的 `Co-Authored-By` trailer。
6. 不用 `--no-verify`、不强推、不设置 `ALLOW_GENERATED_UPDATE`（GIT-4、HAR-4）。
7. shape：不改 `docs/shape/*.md`，写 `docs/shape/inbox/<分支>.md`（HAR-8）。
8. 收尾汇报：提交哈希、FULL 结果（`HARNESS_STATUS` 与证据目录）、未决问题、遗留的临时文件位置。
