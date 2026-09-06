# CURRENT.md

> 章程：工程现场。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 刚收口：Queue 假切歌（行样式 + `cursor_set`）。Playback 打开/预开只留 GUI Task 注释位。
- 下一活动：**讨论**更强颜色方案；未达成前不改 SquareLine、不改固件。先前约定要新开分支再做。
- 本主线必读：`docs/gui_ui_design.md` **4.1**（现用 White1/Blue1/Gray1 与壁纸）、**10.5.3**（Queue 行运行时改色）、ADR-0007。
- 按需查词：**播放列表**。

## 本场交接

- 分支：`main`。HEAD `e28a808`。未提交：游标/假切歌、全高 Tab、窗口 8→12、SquareLine 导出（`GUI/`、`SquareLineProject/`）、`storage_playback_cursor.*`、`Tests/storage_catalog/`。
- `Service/gui` 不得包含 `storage_listbuffer.h` / `storage_playback_cursor.h`。禁止手改 `GUI/`。Queue `create_row()` 须对照导出的 `SongPanel1`。
- 假切歌：`CLICKED` 只刷新当前/非当前样式；GUI Task 每圈先 `ConsumeSelect` 再 `cursor_set`。不解码。点按/滑窗后高亮待上板。
- verify：`storage_catalog`、`gui_task` host PASS。FULL FAIL：生成目录写保护（用户导出相对 HEAD）。Debug hex 已含假切歌。助手禁止设 `ALLOW_GENERATED_UPDATE`。
- 下场第一刀：只讨论 4.1 与要动哪些主题色/对象；先更新 `gui_ui_design.md`，再由用户在 SquareLine 改并导出。运行时 `Blue1`/`White1` 也在 Queue 当前行里。
