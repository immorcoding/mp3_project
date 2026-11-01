# Inbox: impl-177-seek-gui

- 2026-10-31 · gui/music · cite · GUI-10 · “总时长未知时不响应、不置灰”由 Service 在进度条 `PRESSED` 回调里吞掉这次按压实现（`lv_indev_wait_release` 并清掉 `PRESSED` 状态）：LVGL 在发 `PRESSING`（改值）与滚动处理前返回，进度条不跟手、分页也不滚；不用清 `CLICKABLE`，否则按压落到父对象、横滑会翻页。换曲取消拖动走同一机制（`Service_GUI_SeekCancel`）。可否响应由 APP 归约按“有当前曲、非停止、总时长非 0”算出，经 `Service_GUI_SeekEnable` 下发（#177 用户确认的接口）。
- 2026-10-31 · gui/music · cite · GUI-20 · 唱盘归零改为：采信的曲目身份变化、`TrackStartCount` 变化或进入停止；不再看位置变小。`music_bad_file/all_bad_hold` 因此变化：整轮坏文件进入 ALL_BAD 停止时唱盘归零（旧实现位置一直为 0，不归零）。
- 2026-10-31 · gui/ownership · cite · GUI-4 · #177 有 3 个既有场景 8 帧变化（另新增 6 个场景 23 帧）。核对办法：同一 worktree `git stash` 后渲染旧截图，逐帧左右拼图加差异掩膜比对：`music_seek` 与 `music_transport` 的差异只在进度条与时间 Label（总时长未知时不再跟手；拖动中时间预览目标；松手后停在目标而不是回位）；`music_bad_file/all_bad_hold` 只有唱盘角度（进入停止归零，GUI-20）。
