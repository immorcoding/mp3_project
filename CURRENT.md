# CURRENT.md

> 章程：工程现场。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 刚收口：Queue 假数据行已上板确认。`Service/gui/main/queue` 把 `GUI/screens/ui_Main.c` 的 `SongPanel1` 构造摘进 for 循环；导出范本隐藏。
- 当前/非当前：Border Opa、符号 Opa、曲名色、Long mode。不改 Border Width。符号不 HIDDEN。
- 假窗口仍是 4 行、第 0 行当当前曲。尚未接线 `storage_listbuffer`。游标未做。
- 下一活动：GUI Task 消费 READY 窗口，按真实 `Length`/`Buffer[i]` 生成行。
- 本主线必读：10.5.3、`catalog_architecture.md`、ADR-0007。
- 按需查词：**播放列表**、**曲库**。Queue 可见行 ≠ 整表。
- `Service/gui` 不得包含 `storage_listbuffer.h`。

## 本场交接

- 分支：`main`。硬切：Queue 假数据视觉已确认，进入 listbuffer 接线。
- SquareLine：只留单行范本。禁止手改 `GUI/`。范本构造变了对照 `ui_Main.c` 更新 `service_gui_main_queue_create_row()`。
- 工作树可能仍有未提交的 SquareLine 导出；`verify.ps1` 会因生成目录写保护 FAIL。改动在 `Service/`，上板状态未当验收。
- 下场第一刀：GUI Task `request` → 等 READY → 填 Queue 槽位 → 写回 IDLE。公开 `Service_GUI` 仍只有 Init/Process；若要加绑定 Interface，先问。
- `listbuffer` 保持线性窗（`Index` + `Buffer[0..Length)`）。Queue **panel 做成固定槽位**：最多 8 个，不够的 Hidden，滑窗以后再转 head 回收，不在 Storage 里做环形数组。
- 游标未落地；接线后暂可继续第 0 行当当前曲，或先不标。歌手元数据没有，曲名先用路径。
