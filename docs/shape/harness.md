# Harness

Claude Code 与 Codex 双轨共用的章程、Skill、审阅 Agent、Hook 与分层验证。

Next id: HAR-14

## Pillars

- 一份事实源，两个薄入口：正文只写一处，工具目录只放指针或生成物。
- 能机制拦截的不写成文字：闸门优先落在 Git Hook、Agent Hook 与检查脚本。
- 软件证据不冒充板级证据。

## Open questions

- Codex 项目级 Hook 需在 Codex 内信任一次（按内容哈希），Hook 改动后需重新确认；是否需要在克隆说明里提示。

## entrypoints

共同章程、规范源与按需读取。

### Rules

- **HAR-1** · provisional · `AGENTS.md` 是唯一章程；根与子目录的 `CLAUDE.md` 只写 `@AGENTS.md`。_Why:_ Codex 原生读 `AGENTS.md`，Claude Code 读 `CLAUDE.md`。
- **HAR-2** · settled · 仓库若有自定义 Skill/Agent，正文只放 `.agents/skills/` 或 `.agents/reviewers/`，工具包装由 `scripts/sync-agent-config.ps1` 生成；当前评审复用已安装的 `code-review`，仓库不维护专用 reviewer。`settings.json`、`hooks.json` 仍手工维护。_Why:_ 保留单一正文与生成一致性，避免另建重复评审入口。_Check:_ FAST 运行 `sync-agent-config.ps1 -Check`，包括空配置与受管残留检查。
- **HAR-9** · provisional · 根 AGENTS 保留项目与语言约定、shape 使用方式、关键保护边界摘要、条件阅读指针和验收入口；子 AGENTS 只补目录独有操作要求。目录路由描述职责与必读正文，模块 README 维护接口和使用约束；正文一处维护，入口引用已落地的目标。_Why:_ 避免根、子入口与规范重复而漂移。_Source:_ [开发入口决定](https://github.com/immorcoding/mp3_project/issues/12)

- **HAR-12** · provisional · 新增 Skill 仅用于反复出现且高风险的独特操作流程，接口、术语和不变量继续由领域文档维护。 _Why:_ 说明型 Skill 会增加常驻导航负担并复制事实源。 _Source:_ [开发入口决定](https://github.com/immorcoding/mp3_project/issues/12)

## hooks

工具守卫与即时检查。

### Rules

- **HAR-3** · provisional · Agent Hook 两边共用 `scripts/hooks/*.ps1`，同时解析 Claude 的 `file_path` 与 Codex `apply_patch` 的补丁文本，路径以 Hook 输入的 `cwd` 为基准规范化；生成/Vendor 前缀复用 `check-generated-write.ps1`，分层映射复用 `check-layer-includes.ps1`，Hook 专有的 CubeMX 源、配置头与 USER CODE 根只在 `scripts/hooks/agent_hook_helpers.ps1`。 _Why:_ 两种工具的输入不同，但保护边界必须一致且只维护一份。 _Source:_ Codex `core/src/tools/hook_names.rs`（`apply_patch` 以 `Edit`/`Write` 为 matcher 别名）
- **HAR-4** · provisional · PreToolUse 硬拦生成/Vendor 目录写入与 `--no-verify`、强推、`ALLOW_GENERATED_UPDATE`；CubeMX `USER CODE` 文件与生成器配置头在 Claude 走 `ask`、在 Codex 一律 `deny`；PostToolUse 只做毫秒级单文件检查；不在 Stop 跑构建。_Why:_ Codex 对 `ask` 会记错误后放行；固件构建慢，每轮触发不可承受。_Source:_ Codex `hooks/src/events/pre_tool_use.rs`（`unsupported_permission_decision_fails_open`）、commit `e57187b`（强推判定只看 `git push` 之后的参数）

## verification

验证层级与主机自测隔离。

### Rules

- **HAR-5** · settled · 验证分 FAST（pre-commit）→ CHANGED（pre-push）→ FULL（`verify.ps1`）→ HARDWARE；声称完成前 FULL 通过；`PASS_HOST_ONLY` 不是板级验收。 _Why:_ 快速反馈与完整软件证据分层执行，硬件结论另需真实设备。 _Source:_ [verification.md](sources/verification.md) _Check:_ FAST、CHANGED 由 `install-git-hooks.ps1` 安装的 `pre-commit`、`pre-push` 自动运行，路径路由在 `scripts/rules/verification.psd1`；FULL 在声称完成前手动运行。
- **HAR-6** · settled · 自测夹具挂起继承的 `GIT_*` 环境变量；主机编译器只读 `MP3_HOST_CC`。_Why:_ worktree 中 Hook 导出的绝对 `GIT_DIR` 曾让夹具写坏真实仓库；通用 `CC` 常被其他项目设为 Clang。_Source:_ commit `e1be57c` _Check:_ `scripts/build_helpers.ps1` 的 `Suspend-InheritedGitEnvironment` 挂起 `GIT_DIR` 等 7 个仓库定位变量；`test-host.ps1` 只读 `MP3_HOST_CC`。

- **HAR-10** · settled · 非 trivial 功能、修复、重构（含文档与规则迁移）及 Harness 改动统一用 `code-review`，固定 diff 基线，由未参与编写的规范轴与需求轴审阅者分别核对适用规则、需求、范围、验证覆盖及原始证据；硬件敏感变更在规范轴纳入嵌入式清单。作者自查单列；仅拼写、链接等小改可豁免，用户点名时总是审阅；修复后重跑受影响检查，审阅依据改变后复审。_Why:_ 自动化检查与独立判断各司其职，统一流程保留专项风险覆盖。_Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 提供领域指针，审阅报告分别记录发现、验证证据与板级待验事项。
- **HAR-11** · provisional · 验证路径只在 verification.psd1 维护，变更同时更新路由自测。_Why:_ 重复路径表会制造第二份事实源，路由变更需要独立证据。

- **HAR-13** · provisional · 最终验证证据记录提交 SHA、工作树状态、命令、退出码和完整输出；交付时把证据归档到 PR 附件或制品位置，不以系统临时目录作为唯一留存。 _Why:_ 摘要不能独立证明实际验收快照，临时文件也会被清理。 _Source:_ [spec #15](https://github.com/immorcoding/mp3_project/issues/15)

## collaboration

改动授权与领域文件写入位置。

### Rules

- **HAR-7** · provisional · 改动许可交给工具自身模式（Claude 默认/acceptEdits，Codex workspace-write + on-request）与 Hook；不用点火词或许可档位表。_Why:_ 文字许可协议耗上下文且拦不住。
- **HAR-8** · provisional · 领域文件 `docs/shape/*.md` 只在写入分支（默认 `main`）修改，规则编号也只在那里分配；其他分支把 Signal、草稿和用户当场的批准写进 `docs/shape/inbox/<分支>.md`，合并后在 `main` 上按 drain 处理。_Why:_ 并行分支同时改领域文件会冲突，相同的 `Next id` 改动会被 git 静默合并成重号。_Source:_ shape-your-project `INBOX.md`
