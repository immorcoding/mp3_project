# Inbox: ticket/82-sim-real-gui-music

> 以下是 2026-10-18 实施 [#82 模拟器编译真实 gui_music 的等价替换](https://github.com/immorcoding/mp3_project/issues/82) 时记下的 Signal，规则正文未改。drain 时提给用户。

## Signal

- 2026-10-18 · gui · cite · GUI-4 · 基线第 1 步（#72 F5）：模拟器改编译真实 `gui_music`（游标版）+ 真实 `storage_playback_cursor.c` + 假 Storage `fakes/storage_sim.c`。八场景/54 帧中 44 帧哈希不变；变化的 10 帧（`music_transport` 的 `playing_progress`、`paused`，`vinyl_rotation` 的 `play_quarter`、`play_half`、`paused`、`paused_hold`、`resumed`、`next_zero_playing`、`paused_after_next`、`back_to_music`）逐张对比旧图，差异只在进度条填充区（约 x 25–47、y 222–227）：旧演示分区每 300 ms 自走 1% 假进度，真实 `gui_music` 不走表；唱盘角度、播放键图标、Queue 高亮逐像素相同。看图后用 `-Update` 重写这两个场景的基线。
- 2026-10-18 · harness · signal · 无对应规则 · 模拟器替身边界扩到 APP 音乐分区后，`scripts/rules/verification.psd1` 的 `GuiScenarioPathPatterns` 补了模拟器现在编译或替身实现的生产路径（`APP/tasks/gui/music/`、`storage_playback_cursor.[ch]`、`storage_listbuffer.h`、`storage_catalog_config.h`、`storage_task(_config).h`、`Service/filesystem/filesystem_types.h`），路由自测同步。游标退场票（#72 E1）删除 `storage_playback_cursor.*` 时应同时删掉这条模式；假播放后端接入时（#72 F3）应补 `playback_contract.h`。
- 2026-10-18 · gui · signal · 无对应规则 · `G` 键（模拟重扫）在游标版下除了“代次 +1、已显示窗口保留到下一次 request”，还代替 `storage_catalog` 按新代次把游标重置到第 0 首，因此重扫后已显示窗口不再高亮（窗口代次与游标代次不符）。这与固件重扫后的游标行为一致；第 2 步换快照后高亮改按 C3 判定，该副作用随游标退场消失。
