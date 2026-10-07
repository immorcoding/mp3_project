# AGENTS.md

本仓库是 STM32H743 便携式媒体播放器固件。技术文档、代码注释与用户回复使用中文；本文件是 Claude Code 与 Codex 共用入口，同目录 `CLAUDE.md` 只引用它。

## 开始任务

- GitHub spec/ticket：先读事项及评论，再按 [workflow](docs/shape/workflow.md) 与 [tracker](docs/agents/issue-tracker.md) 处理依赖、进度和证据；提交与 PR 读 [git](docs/shape/git.md)。
- 修改文件：先读路径上最近的子 `AGENTS.md`，按 [ROUTES](docs/shape/ROUTES.md) 找目标模块 README、领域规则与技术正文；其他文档从 [文档地图](docs/shape/README.md) 找。
- 跨层、边界或公开接口变化：读 [architecture](docs/shape/architecture.md) 与相关模块 README，分别判断所有权、include 和运行时路径；完成事件沿相关模块 README 核对，C 代码另读 [code-style](docs/shape/code-style.md)。
- GUI 变化：先读 [gui](docs/shape/gui.md)，更新其中受影响的规则或待决设计，再改手写 view。
- 术语按条查 [GLOSSARY](GLOSSARY.md)；长期边界决定与 ADR 读取顺序见 [domain](docs/agents/domain.md)。修改工具配置、Skill 或 reviewer 时读 [harness](docs/shape/harness.md)：规范源在 `.agents/`，包装用 `./scripts/sync-agent-config.ps1` 生成。

## Shape 使用方式

动到哪个领域先读哪个领域，并向用户提出其中 Proposed 条目。**exploring** 照做并记录摩擦；**provisional** 打破前先问；**settled** 强制执行。规则影响改动、挡路、被违反或用户纠正时，记带日期的 Signal；长期规则决定用 `shape-your-project`。
领域文件只在 writer branch（本仓库 `main`）修改；其他分支的 Signal、草稿和批准写入 `docs/shape/inbox/<分支>.md`，合并到 writer branch 后 drain（HAR-8）。

## 关键保护

- CubeMX 生成文件和 Vendor 只经源工程导出或上游更新，不手改；`USER CODE` 唯一例外须先获用户确认且仅薄转发，具体边界见 ARC-2/ARC-3。`FATFS/Target/bsp_driver_user_diskio.*` 是自维护契约。
- 不绕过 Hook：不用 `--no-verify`、不强推、不设置或写入 `ALLOW_GENERATED_UPDATE`（HAR-4、GIT-4）。克隆后执行 `./scripts/install-git-hooks.ps1`。
- 只做授权范围内改动；改公开接口、词典、ADR 或生成器配置头（`lv_conf.h`、`FreeRTOSConfig.h`、`ffconf.h`）前先问用户，已有明确授权沿用。

## 完成前

按 [verification](docs/shape/sources/verification.md) 验证：声称软件验证完成前 `./scripts/verify.ps1`（FULL）必须通过；GUI 变化另跑 `./Tools/gui_simulator/run-scenarios.ps1`，基线变更遵守 GUI-4。
`PASS_HOST_ONLY`、`NEEDS_HARDWARE_VALIDATION` 和 PC 像素等价都不是板级验收；待上板事项标 `hw:pending`。非 trivial 变化调用 `independent-verifier`；涉及 DMA、Cache、ISR、RTOS、HAL、Platform、链接段或硬件生命周期再调用 `embedded-reviewer`，触发细则见 [harness](docs/shape/harness.md#verification)。
