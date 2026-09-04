# CURRENT.md

> 章程：工程现场。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 当前活动：文档按代码全库同步。GUI 播放列表已搁置，本场不启动。
- 固件最近落地：曲库与顺序播放列表（2026/9/3）。技术事实见 [catalog_architecture.md](docs/catalog_architecture.md)；卷分工见 [ADR-0015](docs/adr/0015-volume-roles-and-resource-install.md)。
- 活动 scratch：无。`.scratch/settings_backdrop_prototype/` 是旧 GUI 草稿，不是当前主线。
- 阻塞：无。
- 本主线必读：`docs/catalog_architecture.md`、`docs/adr/0014-filesystem-volume-aware-file-interface.md`、`docs/adr/0015-volume-roles-and-resource-install.md`、`APP/tasks/storage/README.md`、`Service/filesystem/README.md`。
- 本主线之后：用户新开窗口继续 GUI 播放列表。尚未排期：Flash FTL 板级掉电验收（[flash_ftl_design.md](docs/flash_ftl_design.md)）、音频解码。
- 按需查词：曲库 / 播放列表 → **曲库**、**播放列表**；存储 → **文件系统 Module**、**存储任务**。

## 本场交接

- 分支：`main`。本场只改文档与 Module README，不改产品代码、不手改 `GUI/`。
- 上场：用户搁置 GUI 播放列表，要求文档以代码为准。曲库不存来源卷（Flash 存歌已取消）；Flash 自动格式化由 `STORAGE_FLASH_AUTO_FORMAT` 控制。
- 未决：无。
- 下场第一刀：文档同步完成后，用户新开窗口做 GUI 播放列表；先核 [gui_ui_design.md](docs/gui_ui_design.md)（原型仍不接线真实扫描）。
- `verify.ps1`：已通过（分层 include、生成目录写保护、固件 Debug/Release、主机回归）。
