# Now Playing 三键与进度条

`transport/` 是 Main Screen 的私有播放控制 Module。它在界面创建后给 `view/` 创建的三键挂 `CLICKED`、给进度条挂 `RELEASED`，把命令写入 GUI 输入单槽；按 `Service_GUI_TransportApply(playing)` 改 PLAY/PAUSE 符号，按 `Service_GUI_ProgressApply(percent)` 改进度条，按 `Service_GUI_TimeApply(position_ms, duration_ms)` 改时间 Label。

不包含 `storage_playback_cursor.h` 或 `playback_contract.h`。事件只在本 Module 绑定。拖动期间若 Slider 为 `PRESSED`，`ProgressApply` 不写回，避免和手指抢值；时间 Label 照常写入真实时间。时间 Label 由 `view/` 创建（不写文本），本 Module 在 prepare 时按同一格式写入初始 `0:00/--:--`，此后文本未变不重写。假唱盘第一帧与旋转由 `main/vinyl/` 负责（`Service_GUI_VinylApply`），本 Module 不碰唱盘。唱片底图来自 Resource Pack，不编进固件。

## 私有 Interface

- `service_gui_main_transport_prepare()`：仅由 `service_gui_main_prepare()` 在 Queue 之后、Background 之前调用。
- `service_gui_main_transport_apply(playing)`：仅由 `Service_GUI_TransportApply()` 转发。
- `service_gui_main_transport_apply_progress(percent)`：仅由 `Service_GUI_ProgressApply()` 转发。
- `service_gui_main_transport_apply_time(position_ms, duration_ms)`：仅由 `Service_GUI_TimeApply()` 与本 Module 的 prepare 调用。
