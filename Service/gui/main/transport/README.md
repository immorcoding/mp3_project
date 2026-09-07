# Now Playing 三键

`transport/` 是 Main Screen 的私有三键 Module。它在 `ui_init()` 之后给导出按钮挂 `CLICKED`，把命令写入 GUI 输入单槽，并按 `Service_GUI_TransportApply(playing)` 改 `MusicPlayPauseIcon` 的 PLAY/PAUSE 符号。

不包含 `storage_playback_cursor.h`。不在 SquareLine 里添加事件。进度条本 Module 不处理。

## 私有 Interface

- `service_gui_main_transport_prepare()`：仅由 `service_gui_main_prepare()` 在 Queue 之后、Background 之前调用。
- `service_gui_main_transport_apply(playing)`：仅由 `Service_GUI_TransportApply()` 转发。
