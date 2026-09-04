# CURRENT.md

> 章程：工程现场。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 当前活动：暂停。Queue 窗口单槽已落地，GUI 播放列表未接线。
- 固件最近落地：曲库、顺序播放列表、`storage_listbuffer` 单槽（`request`/`load`）。技术事实见 [catalog_architecture.md](docs/catalog_architecture.md)。
- 活动 scratch：无。
- 阻塞：无。等用户 SquareLine 导出 Queue 列表骨架后再涂行。
- 本主线必读：`docs/gui_ui_design.md`、`docs/catalog_architecture.md`、`docs/coding_standard.md` 第 2 节、`APP/tasks/storage/README.md`、`APP/tasks/storage/catalog/storage_listbuffer.h`。
- 按需查词：曲库 / 播放列表 → **曲库**、**播放列表**。
- `verify.ps1`：已通过（分层 include、生成目录写保护、固件 Debug/Release、主机回归）。

## 本场交接

- 分支：`main`。本场不手改 `GUI/`。
- 上场：宏归入各目录 `config.h`（行末注释、按文件分组）；listbuffer 做成 GUI `request` + Storage `load`；通知数组扩到 4。用户要求暂停并提交。
- 未决：滚动时上一窗未回是丢弃还是合并最新起点（当前非 `IDLE` 的 `request` 返回 `STORAGE_ERROR`）。
- 下场第一刀：用户 SquareLine 建 Queue 行模板并导出；GUI Task 轮询 `READY` 涂 List，读完把 `Status` 写回 `IDLE`。`Service/gui` 不得包含 listbuffer 头。
