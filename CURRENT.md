# CURRENT.md

> 章程：工程现场。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 刚收口：Queue 假切歌已在 `main` `97d315a`。
- 本场：`gui-theme`。SquareLine 五全局色已改名并导出。Queue 运行时已跟 `Accent`/`Ink`。`ThemeApply` 与纯色背景尚未做。
- 本主线必读：`docs/gui_ui_design.md` **4.1**（占位 hex）、**10.5.3**、ADR-0007。
- 按需查词：**平台电源**。

## 本场交接

- 分支：`gui-theme`。禁止手改 `GUI/`。`Service/gui` 不得包含 `storage_listbuffer.h` / `storage_playback_cursor.h`。
- 占位：`Accent=#00B0DE`、`Ink=#F1F6FF`、`Muted=#404040`、`Wash=#E7E7E7`、`Ground=#000000`。真值将放 Service 表；不在 SquareLine 加 Theme 2。
- 已赞成：纯色关壁纸、Tab 只留色层；Boot 本轮不改；不要 `gui_system` 装电量/时间。
- `Ground=#000000` 与 LVGL 默认黑相同；按 hex 匹配前建议在 SquareLine 改成更独特占位（如 `#13223D`）再导出。
- 下场第一刀：Service 调色板宏 + `ThemeApply`（刷占位色、壁纸显隐、毛玻璃）。先不要做电量/时间。
