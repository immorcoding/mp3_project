# Now Playing 三键与进度条

`transport/` 是 Main Screen 的私有播放控制 Module。它在 `ui_init()` 之后给导出按钮挂 `CLICKED`、给 `MusicPlayingSlider` 挂 `RELEASED`，把命令写入 GUI 输入单槽；按 `Service_GUI_TransportApply(playing)` 改 PLAY/PAUSE 符号，按 `Service_GUI_ProgressApply(percent)` 改进度条。

不包含 `storage_playback_cursor.h`。不在 SquareLine 里添加事件。拖动期间若 Slider 为 `PRESSED`，`ProgressApply` 不写回，避免和手指抢值。`MusicPlayerTimeLabel` 已导出，本 Module 暂不改其文本。唱片旋转待 10.5.4 对象导出后再接到 `TransportApply`。唱片底图下一版从 Resource Pack 绑定，本版不把 PNG 编进 `GUI/`。

## 私有 Interface

- `service_gui_main_transport_prepare()`：仅由 `service_gui_main_prepare()` 在 Queue 之后、Background 之前调用。
- `service_gui_main_transport_apply(playing)`：仅由 `Service_GUI_TransportApply()` 转发。
- `service_gui_main_transport_apply_progress(percent)`：仅由 `Service_GUI_ProgressApply()` 转发。
