# Harness

Claude Code 与 Codex 双轨共用的章程、Skill、审阅 Agent、Hook 与分层验证。

Next id: HAR-8

## Pillars

- 一份事实源，两个薄入口：正文只写一处，工具目录只放指针或生成物。
- 能机制拦截的不写成文字：闸门优先落在 Git Hook、Agent Hook 与检查脚本。
- 软件证据不冒充板级证据。

## Rules

- **HAR-1** · provisional · `AGENTS.md` 是唯一章程；根与子目录的 `CLAUDE.md` 只写 `@AGENTS.md`。_Why:_ Codex 原生读 `AGENTS.md`，Claude Code 读 `CLAUDE.md`。
- **HAR-2** · settled · Skill 正文在 `.agents/skills/`，审阅 Agent 正文在 `.agents/reviewers/`；`.claude/` 与 `.codex/` 下的 Skill 与审阅 Agent 只放 `scripts/sync-agent-config.ps1` 生成的包装（`settings.json`、`hooks.json` 手工维护，不在此列）。_Why:_ 两套工具目录约定不同，手工双写必然漂移。_Check:_ FAST 运行 `sync-agent-config.ps1` 一致性校验。
- **HAR-3** · provisional · Agent Hook 两边共用 `scripts/hooks/*.ps1`，同时解析 Claude 的 `file_path` 与 Codex `apply_patch` 的补丁文本，路径以 Hook 输入的 `cwd` 为基准规范化；生成/Vendor 前缀复用 `check-generated-write.ps1`，分层映射复用 `check-layer-includes.ps1`，Hook 专有的 CubeMX 源、配置头与 USER CODE 根只在 `scripts/hooks/agent_hook_helpers.ps1`。_Source:_ Codex `core/src/tools/hook_names.rs`（`apply_patch` 以 `Edit`/`Write` 为 matcher 别名）
- **HAR-4** · provisional · PreToolUse 硬拦生成/Vendor 目录写入与 `--no-verify`、强推、`ALLOW_GENERATED_UPDATE`；CubeMX `USER CODE` 文件与生成器配置头在 Claude 走 `ask`、在 Codex 一律 `deny`；PostToolUse 只做毫秒级单文件检查；不在 Stop 跑构建。_Why:_ Codex 对 `ask` 会记错误后放行；固件构建慢，每轮触发不可承受。_Source:_ Codex `hooks/src/events/pre_tool_use.rs`（`unsupported_permission_decision_fails_open`）、commit `e57187b`（强推判定只看 `git push` 之后的参数）
- **HAR-5** · settled · 验证分 FAST（pre-commit）→ CHANGED（pre-push）→ FULL（`verify.ps1`）→ HARDWARE；声称完成前 FULL 通过；`PASS_HOST_ONLY` 不是板级验收。_Source:_ [verification.md](../verification.md) _Check:_ FAST、CHANGED 由 `install-git-hooks.ps1` 安装的 `pre-commit`、`pre-push` 自动运行，路径路由在 `scripts/rules/verification.psd1`；FULL 在声称完成前手动运行。
- **HAR-6** · settled · 自测夹具挂起继承的 `GIT_*` 环境变量；主机编译器只读 `MP3_HOST_CC`。_Why:_ worktree 中 Hook 导出的绝对 `GIT_DIR` 曾让夹具写坏真实仓库；通用 `CC` 常被其他项目设为 Clang。_Source:_ commit `e1be57c` _Check:_ `scripts/build_helpers.ps1` 的 `Suspend-InheritedGitEnvironment` 挂起 `GIT_DIR` 等 7 个仓库定位变量；`test-host.ps1` 只读 `MP3_HOST_CC`。
- **HAR-7** · provisional · 改动许可交给工具自身模式（Claude 默认/acceptEdits，Codex workspace-write + on-request）与 Hook；不用点火词或许可档位表。_Why:_ 文字许可协议耗上下文且拦不住。

## Open questions

- Codex 项目级 Hook 需在 Codex 内信任一次（按内容哈希），Hook 改动后需重新确认；是否需要在克隆说明里提示。

## Signals

- 2026-10-02 · friction · HAR-4 · 只读的 `git config --get core.hooksPath` 也被拦截。
