# CURRENT.md

> 章程：工程现场。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 刚收口：Default/Solid 过滤器外观；LVGL 堆 256 KiB；GUI 栈 2048 word。进度条按现导出：Muted Opa 150 + 1px Wash 边 Opa 40。
- 下一活动：提交本分支（生成目录需维护者会话 `ALLOW_GENERATED_UPDATE`）。未接电量/时间。
- 本主线必读：`Service/gui/theme/README.md`、`docs/gui_ui_design.md` **4.1**、ADR-0007 第 6 条。
- 按需查词：**平台电源**。

## 本场交接

- 分支：`gui-theme`。禁止手改 `GUI/`。不调用 `ui_theme_set()`。不要 `gui_system`。
- 公开：`Service_GUI_ThemeApply(id)`。`SERVICE_GUI_THEME_STARTUP` 现为 Solid。
- Default 五色宏等于占位 hex；Solid 另表。主机：`Tests/gui_theme/`。
- Init 结束后重对 `lv_tick`。Boot→Reveal / Reveal→Lock 仍走 SquareLine Fade。
- 768 word GUI 栈会在 Default 解锁 Fade 溢出。Boot Arc 未改。
