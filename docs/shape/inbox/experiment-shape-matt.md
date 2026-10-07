# Inbox: experiment/shape-matt

- 2026-10-07 · gui/transport · cite · GUI-10 · 唱盘旋转只消费 APP 既有 playing 与游标事件，Service 不读游标、不按墙钟走表。
- 2026-10-07 · gui/transport · cite · GUI-14 · 旋转沿用 vinyl 合成后的同一 Image 与 SDRAM 缓冲，不新增资源，角度 0 时像素与原基线一致。
- 2026-10-07 · gui/ownership · cite · GUI-3 · 唱盘旋转先在本 inbox 写设计约束，再改 `main/vinyl/`；view 对象树未改。
- 2026-10-07 · gui/regression · cite · GUI-4 · 新增 `vinyl_rotation` 场景；既有七场景截图逐帧检查后只重写受旋转影响的帧。
- 2026-10-07 · gui/transport · proposed · provisional · 唱盘旋转由 `Service_GUI_VinylApply(playing, reset_angle)` 驱动：playing 为真从当前角度顺时针匀速转，`SERVICE_GUI_MAIN_VINYL_REVOLUTION_MS`（6 s）一圈；为假删动画并保留角度；`reset_angle` 在切歌与清空时把角度归零。同一 Image 任一时刻至多一条 lv_anim，Main Screen 不销毁故不需析构；APP 在游标变化、playing 变化和 CLEAR 时调用，Service 不读游标。_Why:_ 旋转是播放反馈而非进度，角度状态只能由 LVGL Image 持有，切歌需要一个独立于 playing 与进度的信号。_Source:_ [唱盘旋转场景](../../../Tools/gui_simulator/scenarios/vinyl_rotation.expected)、[spec #22](https://github.com/immorcoding/mp3_project/issues/22)
- 2026-10-07 · gui · proposed · 解决 Open question「唱盘旋转……何时接入」中的旋转部分：playing/paused/CLEAR 行为已由 `Service_GUI_VinylApply` 固定；ID3 封面、真实时间与音频进度仍待 Playback。
- 2026-10-07 · gui/regression · friction · GUI-4 · gui.regression.md 的 References 仍写「七场景/39帧」；加入 `vinyl_rotation` 后为八场景/54 帧，drain 时更新该句。
- 2026-10-07 · gui/lock · friction · new · Lock 的呼吸提示动画在解锁后仍无限运行（Lock Screen 未删除），空闲时 `lv_anim_count_running()` 恒为 1；不属本票范围，待决定是否在 SCREEN_UNLOADED 时停掉。
