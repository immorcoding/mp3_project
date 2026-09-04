# CURRENT.md

> 章程：工程现场。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 当前活动：仓库 agent 外部记忆（根上 `AGENTS.md` / `CONTEXT.md` / `CURRENT.md`），替代对话压缩摘要。
- 固件最近落地：曲库与乐谱（2026/9/3）。Filesystem 卷感知与卷分工见 [ADR-0014](docs/adr/0014-filesystem-volume-aware-file-interface.md)、[ADR-0015](docs/adr/0015-volume-roles-and-resource-install.md)。
- 活动 scratch：无（本场直接改根文档）。`.scratch/settings_backdrop_prototype/` 是旧 GUI 草稿，不是当前主线。
- 阻塞：无。
- 本主线必读：`docs/agents/domain.md`、`docs/README.md`。
- 本主线之后：回到播放器功能。尚未排期：Flash FTL 板级掉电验收（[flash_ftl_design.md](docs/flash_ftl_design.md)）、音频解码；若继续曲库/GUI，先核对 [gui_ui_design.md](docs/gui_ui_design.md) 是否仍写「原型不接线真实扫描」。
- 按需查词：曲库 / GUI → **曲库**、**播放列表**；存储 → **文件系统 Module**。

## 本场交接

- 分支：`main`。本场改：`AGENTS.md`、`CURRENT.md`、`CONTEXT.md`、`README.md`、`docs/README.md`、`docs/agents/domain.md`、`docs/agents/issue-tracker.md`。
- 上场：三入口落盘后，按章程瘦 `CONTEXT.md` 越界条（实现参数未迁出，目标架构文档里已有）；根 `README.md` 改为四入口。
- 未决：固件「下一刀」用户未点名；不要擅自开产品功能或新建 `.scratch/`。
- 下场第一刀：用户审「进度」是否符合真实主线；需要则改正度（先问）后硬切开新对话。
- `verify.ps1`：已通过（分层 include、生成目录写保护、固件 Debug/Release、主机回归）。
