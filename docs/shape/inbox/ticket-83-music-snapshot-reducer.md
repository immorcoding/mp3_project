# Inbox: ticket/83-music-snapshot-reducer

> 以下是 2026-10-18 实施 [#83 音乐页快照归约](https://github.com/immorcoding/mp3_project/issues/83) 时记下的 Signal；规则正文改写未获批准。drain 时提给用户。

## Signal

- 2026-10-18 · harness · signal · 无对应规则 · `scripts/rules/verification.psd1` 的 `gui_task` 路径不含 `APP/tasks/playback/playback_contract.h`，而 `gui_music_transport` 现在直接包含它（`Tests/gui_task` 的归约测试也依赖它）。合同头变化时 CHANGED 不会选中 `gui_task`，FULL 才能发现。建议把 `^APP/tasks/playback/playback_contract\.h$` 加进 `gui_task` 的 `PathPatterns`（需同步 `scripts/harness_helpers.Tests.ps1`），本票没有改 harness 配置。
- 2026-10-18 · gui · cite · GUI-17、GUI-10 · 归约器按 C1 把“冻结”做在整份快照上：身份、三态、停止原因、总时长取最后一份已追上的快照，位置与两个计数取当前份。C6 的“进入停止”以该冻结快照的状态加当前 `StopCount` 判定（StopCount 变化即重新取基准），因此原因不变地再次停止（AUDIO_FAULT 锁存后再按播放）会重新显示短语，与 C7 同圈的闪烁配合。
- 2026-10-18 · gui · signal · GUI-20 · C8 把“曲库代次从 0（开启尚未确认）变为非 0”当作身份变化，会在开播后多归零一次唱盘角度；此时位置本来接近 0，视觉上无差别，没有特殊处理。若 #84 的场景帧显示异常，再改为只比较 `HasTrack` 与 `Index`。
- 2026-10-18 · architecture · signal · 无对应规则 · `gui_music_queue_window_current_index()`（旧游标版高亮）及其测试保留不删：`gui_music.c` 在 #84 接线前仍调用它，删除放进 #84。`gui_music.c` 为保持可编译，暂自持旧的 playing 与假进度（文件内有“过渡（#83）”注释），#84 删除。
