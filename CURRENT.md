# CURRENT.md

> 章程：工程现场。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 刚收口：Queue 已接线 `storage_listbuffer` 第一窗。`request(Index=0, 至多 8)`；READY 后 `QueueApply` 按 `Length` 填行，写回 IDLE。拔卡清空。
- 曲名暂为路径原文，歌手空。`request` 只带 Index/条数/代次。ID3 等 `load` 调解析器，见 `catalog_architecture.md` 第 5 节。游标未做，第 0 行当当前曲。
- 下一活动：**Queue 滑窗**（滚动后再 `request` 新 `Index`）。不要做 ID3、不要做播放列表游标。
- 本主线必读：10.5.3、`catalog_architecture.md`、ADR-0007。
- 按需查词：**播放列表**、**曲库**。Queue 可见行 ≠ 整表。

## 本场交接

- 分支：`main`。硬切去滑窗。`gui_task.c` 的 REQUEST 仍写死 `Index=0`。
- 滑窗：`QueueTab` 竖滑后改 `Index` 再 `request`；已有行转 head 改字，不无限 `create`。不要把 listbuffer 改成环形数组。`Service/gui` 不得包含 `storage_listbuffer.h`。
- SquareLine：只留单行范本。禁止手改 `GUI/`。范本变了对照 `ui_Main.c` 更新 `create_row()`。
- 行高已锁一行：当前行循环滚，其它行 Dot。当前行右侧是 `LV_SYMBOL_AUDIO`，范本 `S` 只占位。拔卡看卷是否挂载；QueueApply 失败重试。
- 不要裁 `Music/` / `.mp3`，不要按文件名切歌手。重插跳回当前曲要记路径，不要歌名哈希。
- 上板：第一窗路径原文；滑窗未做。Host `verify.ps1` 曾 `PASS_HOST_ONLY`，不能当板级验收。
