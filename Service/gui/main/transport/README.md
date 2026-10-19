# Now Playing 三键与进度条

`transport/` 是 Main Screen 的私有播放控制 Module。它在界面创建后给 `view/` 创建的三键挂 `CLICKED`、给进度条挂 `RELEASED`，把命令写入 GUI 输入单槽；按 `Service_GUI_TransportApply(playing)` 改 PLAY/PAUSE 符号，按 `Service_GUI_ProgressApply(percent)` 改进度条，按 `Service_GUI_TimeApply(position_ms, duration_ms)` 记下时间文本。

不包含 `playback_contract.h`。事件只在本 Module 绑定。拖动期间若 Slider 为 `PRESSED`，`ProgressApply` 不写回，避免和手指抢值；时间照常记下真实时间。时间 Label 由 `view/` 创建（不写文本），本 Module 在 prepare 时按同一格式写入初始 `0:00/--:--`。假唱盘第一帧与旋转由 `main/vinyl/` 负责（`Service_GUI_VinylApply`），本 Module 不碰唱盘。唱片底图来自 Resource Pack，不编进固件。

## 状态行

时间 Label 兼作状态行（GUI-19、#72 D2–D5），优先级“常驻文本 > 跳过提示 > 时间”只在本 Module 实现：

- 常驻文本只存在 Label 里（Label 拷贝），本 Module 只记“是否常驻”；设置常驻时删除跳过提示计时器并清零累加数。
- 跳过提示用一个单次 `lv_timer`（`SERVICE_GUI_MUSIC_SKIP_NOTICE_MS`），再次调用时累加并 `lv_timer_reset` 重新计时；到期回调只清句柄与累加数后刷新，计时器由 LVGL 按重复次数删除。
- 闪烁用以时间 Label 为 var 的 `lv_anim` 改 `text_opa`（淡出再淡回，共 `SERVICE_GUI_MUSIC_STATUS_FLASH_MS`）；重复调用先删旧动画并恢复不透明，所以至多一条。
- 刷新时与 Label 现有文本比较，相同不重写，避免每圈无谓重绘。

## 私有 Interface

- `service_gui_main_transport_prepare()`：仅由 `service_gui_main_prepare()` 在 Queue 之后、Background 之前调用。
- `service_gui_main_transport_apply(playing)`：仅由 `Service_GUI_TransportApply()` 转发。
- `service_gui_main_transport_apply_progress(percent)`：仅由 `Service_GUI_ProgressApply()` 转发。
- `service_gui_main_transport_apply_time(position_ms, duration_ms)`：仅由 `Service_GUI_TimeApply()` 与本 Module 的 prepare 调用。
- `service_gui_main_transport_apply_status(text)`、`service_gui_main_transport_apply_skip_notice(added)`、`service_gui_main_transport_flash()`：分别仅由 `Service_GUI_StatusApply()`、`Service_GUI_SkipNoticeApply()`、`Service_GUI_StatusFlash()` 转发。
