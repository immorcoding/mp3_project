# Now Playing 三键与进度条

`transport/` 是 Main Screen 的私有播放控制 Module。它在界面创建后给 `view/` 创建的三键挂 `CLICKED`、给进度条挂 `PRESSED`/`PRESSING`/`RELEASED`/`PRESS_LOST`，把命令与进度条手势（按下/拖动/松手/取消，千分比）写入 GUI 输入单槽；按 `Service_GUI_TransportApply(playing)` 改 PLAY/PAUSE 符号，按 `Service_GUI_ProgressApply(percent)` 改进度条，按 `Service_GUI_TimeApply(position_ms, duration_ms)` 记下时间文本。

不包含 `playback_contract.h`。事件只在本 Module 绑定。拖动期间若 Slider 为 `PRESSED`，`ProgressApply` 不写回，避免和手指抢值；时间 Label 显示调用方给的时间（拖动中为预览）。

## 进度条手势

- 进度条量程为 0..`SERVICE_GUI_SEEK_SCALE`（1000，view 设置），手势事件的 param 直接取 Slider 值；`ProgressApply` 的百分比乘 10 写入。
- `PRESSED` post `SEEK_PRESS`，此时值尚未按触点更新；同一次读触摸紧跟的 `PRESSING`（post `SEEK_DRAG`）在输入槽里与按下合并成最新值。`RELEASED` post `SEEK_RELEASE`，`PRESS_LOST` post `SEEK_CANCEL`。
- **吞掉按压**：不响应（`SeekEnable(false)`）时在 `PRESSED` 回调里，或 `SeekCancel()` 时，对 Pointer 输入设备调 `lv_indev_wait_release()` 并清掉 Slider 的 `PRESSED` 状态，记“已吞掉”。LVGL 随即在发 `PRESSING`（改值）与滚动处理之前返回，所以进度条不跟手、父分页不滚动，也画不出按下外观；手指抬起时只收到 `PRESS_LOST`，本 Module 据“已吞掉”不 post，然后清除标志。
- 关掉响应时若正在拖动，按 `SeekCancel` 结束。Init 后不响应，等 APP 按快照打开。时间 Label 由 `view/` 创建（不写文本），本 Module 在 prepare 时按同一格式写入初始 `0:00/--:--`。假唱盘第一帧与旋转由 `main/vinyl/` 负责（`Service_GUI_VinylApply`），本 Module 不碰唱盘。唱片底图来自 Resource Pack，不编进固件。

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
- `service_gui_main_transport_seek_enable(enabled)`、`service_gui_main_transport_seek_cancel()`：分别仅由 `Service_GUI_SeekEnable()`、`Service_GUI_SeekCancel()` 转发。
- `service_gui_main_transport_apply_time(position_ms, duration_ms)`：仅由 `Service_GUI_TimeApply()` 与本 Module 的 prepare 调用。
- `service_gui_main_transport_apply_status(text)`、`service_gui_main_transport_apply_skip_notice(added)`、`service_gui_main_transport_flash()`：分别仅由 `Service_GUI_StatusApply()`、`Service_GUI_SkipNoticeApply()`、`Service_GUI_StatusFlash()` 转发。
