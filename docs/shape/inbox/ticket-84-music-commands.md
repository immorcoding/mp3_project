# Inbox: ticket/84-music-commands

> 以下是 2026-10-18 实施 [#84 音乐页改发命令、读播放快照](https://github.com/immorcoding/mp3_project/issues/84) 时记下的 Signal；规则正文未改。drain 时提给用户。

## Signal

- 2026-10-18 · gui · cite · GUI-4 · 基线第 2 步（#72 F5）：模拟器换上实现合同头的假播放后端 `fakes/playback_sim.c`，`gui_music` 改为快照驱动。旧八场景/54 帧中 35 帧变化（`boot_lock` 全部与 `vinyl_rotation` 的 `books_while_playing` 等 19 帧不变），逐张与旧图对比后 `-Update`：(1) 所有显示 Now Playing 的帧时间 Label 由写死的 `1:00/3:14` 变为快照时间（开机 `0:00/--:--`，播放中如 `0:02/2:30`）；(2) `queue`/`theme_default`/`theme_toggle` 的 Queue 帧开机不再高亮第 0 首（K4：开机无当前曲，旧版由假 Storage 初始化游标）；(3) 播放中帧的唱盘角度略小、进度条有真实填充：开启约 100 ms 后才有位置，且代次确认时归零一次（见下条）；(4) `queue` 的 `playing_again` 点行后正在播放（SELECT 为“选中并播放”，K3）；(5) `music_transport` 的 `seek_released` 松手回位到快照位置（B3）。新增 `music_snapshot`、`music_paused_switch`、`music_echo`、`music_seek`、`music_card_removed`，现为 13 场景/92 帧；`gui.regression.md` References 中的“八场景/54帧”已过期。
- 2026-10-18 · gui · signal · GUI-20 · #83 Signal 的跟进：C8 把代次 0 -> 非 0 当作身份变化。在模拟器中开播后约 100 ms（假后端开启应答）唱盘从约 6° 退回 0°；静帧看不出异常，动态是起转瞬间的一次小回跳。真机开启应答（打开文件、读首帧）可能慢于 100 ms，回跳角度随之变大（每 100 ms 约 6°）。建议决定：前一份代次为 0 且 `HasTrack`、`Index` 不变时不算身份变化（归约测试 `test_play_from_boot_confirms_generation_after_opening` 目前按现行 C8 断言会归零，改规则时同步改这条断言）。
- 2026-10-18 · gui · signal · 无对应规则 · `docs/shape/gui.md` Open questions 的“过渡说明（2026-10-09）”按 #72 G 节应在实施完成后删除；本票完成了 GUI-10、GUI-17、GUI-20 的目标态，GUI-19（状态行）仍待 #86，建议 #86 合入后删除该条。
- 2026-10-18 · gui · signal · GUI-1 · #72 D0 写“view 句柄新增 `time_label`，初始文本改为 `0:00/--:--`”。实现中 view 只创建 Label 并发布句柄、不写文本；初始 `0:00/--:--` 由 `main/transport` 在 prepare 时用 `Service_GUI_TimeApply` 的同一格式化函数写入，避免两处各写一份时间文本。帧上结果相同。
- 2026-10-18 · gui · signal · GUI-17 · 回显未追上时若 Queue 换窗（APPLY），新窗口按“最后一份已追上的快照”套高亮（与 B6 撤回同一规则），此前 Service 对待处理 SELECT 的即时高亮会被覆盖，直到回显追上再按快照重套。#72 C3 只规定“未追上时不覆盖即时高亮”，未写换窗的情况；本票取上述做法，模拟器单线程下需挂起后端再滚动换窗才能看到。
- 2026-10-18 · harness · signal · 无对应规则 · `GuiScenarioPathPatterns` 去掉 `storage_playback_cursor.[ch]`（模拟器不再编译游标），补 `playback_contract.h`、`playback_task_config.h`（假后端取 FIFO 深度）与 `Service/log/log_service(_config).h`（`fakes/log_sim.c` 实现 `Service_Log_Post`），路由自测同步。#83 Signal 提出的 `gui_task` 主机模块路径缺 `playback_contract.h` 仍未改。
- 2026-10-18 · architecture · signal · 无对应规则 · `gui_music.c` 在命令队列满时调用 `Service_Log_Post()`（B6“记日志”），这是 APP 音乐分区第一次依赖 `Service/log`；`APP/tasks/gui/music/README.md` 编译期依赖已补。
