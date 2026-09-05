# CURRENT.md

> 章程：工程现场。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 当前活动：Queue 播放列表 UI 设计。假数据行骨架；本场不接线 `storage_listbuffer`。
- 固件已落地：曲库、顺序播放列表、Queue 窗口单槽、SD 三态（未就绪 / 消抖中 / 就绪）。事实见 [catalog_architecture.md](docs/catalog_architecture.md)、[sd_architecture.md](docs/sd_architecture.md)、`APP/tasks/storage/README.md`。
- 活动 scratch：无。
- 阻塞：无。热插拔仍欠上板，不挡本场 SquareLine。QueueTab 现为空白内容区。
- 本主线必读：`docs/gui_ui_design.md` 第 6.1、10.5（尤其 QueueTab）、`docs/catalog_architecture.md`、`docs/coding_standard.md` 第 2 节、ADR-0007。
- 按需查词：播放列表 / 曲库 / GUI → **播放列表**、**曲库**。Queue 可见行 ≠ 播放列表整表。
- `verify.ps1`：Storage 最近一次 FULL 为 `PASS_HOST_ONLY`。本场是 UI 设计，不冒充固件验收。

## 本场交接

- 分支：`main`。任务已替换：Storage SD 状态机收口 → Queue 播放列表 UI。
- Storage 收口：`Storage_TaskSdStateTypeDef`；`storage_task_sd_is_ready()`；`request` 仅 SD 已挂载；状态不由播放列表代次决定。工作树若仍脏，只应是无关的 `.vscode/` 或 `AGENTS.md`。
- 本场可写：先改 `docs/gui_ui_design.md`，再给 SquareLine 步骤。禁止手改 `GUI/`、`SquareLineProject/`。
- 设计约束：Queue 只绑可见窗口（宏 `STORAGE_LISTBUFFER_MAX_ENTRIES` 现为 12，行数待设计确认）；不把整表指针交给 GUI；首版仍用假数据，不接 `storage_listbuffer_*`。
- 现况：`NowPlayingTab` / `QueueTab` / `LibraryTab` 均为空内容区；播放器区已有假 Slider 与三按钮。SquareLine 导出工具仍为 1.6.1。
- 下场第一刀：在 `gui_ui_design.md` 写明 Queue 行层级与假数据内容，用户再在 SquareLine 里搭 `QueueTab`。
