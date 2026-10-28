# Inbox: impl/149-queue-noto (#149 Queue 曲名与歌手改用资源包 Noto 12 px)

- 2026-10-28 · gui/music · cite · GUI-24 · Queue 两个 Label 与唱盘页曲名取同一个 `service_gui_font_track_text()`；`--font-unavailable` 下三者都回退 Montserrat 12，Queue 行高随回退字体的行高变矮（`queue_font_fallback`）。函数在 0.7.0 叫 `service_gui_font_title()`，现改名 `service_gui_font_track_text()`（曲目元数据字体，GUI-24）。
- 2026-10-28 · gui/ownership · cite · GUI-4 · #149 有 9 个既有场景 20 帧变化（另新增 3 个场景 5 帧）。核对办法：改动前在同一 worktree 跑全部场景并保存截图，先只加假标签（位置 8 中文标题 + 歌手、位置 9 加长歌手）确认只有 Queue 帧变化、唱盘页场景全部不变，再换字体，逐帧左右拼图比对：行内容、当前行高亮、点按命中的行都与改动前一致，差异只有字体、行高（41 → 46 px）与位置 8 的中文标签。
