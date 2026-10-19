# Inbox: ticket/86-status-line

> 以下是 2026-10-19 实施 [#86 状态行：停止原因、跳过提示与重复停止闪烁](https://github.com/immorcoding/mp3_project/issues/86) 时记下的 Signal 与待办；规则正文未改。drain 时提给用户。

## Signal

- 2026-10-19 · gui · cite · GUI-19 · 按 #72 D2–D5 实现：优先级“常驻文本 > 跳过提示 > 时间”只在 `Service/gui/main/transport` 内；跳过提示为单次 `lv_timer`（2000 ms，累加时 `lv_timer_reset`），闪烁为以时间 Label 为 var 的 `lv_anim`（`text_opa` 从 COVER 淡到 TRANSP 再淡回，共 300 ms，ease_in_out），重复调用先删旧动画。模拟器日志证据：`music_stop_reasons` 连按两次播放后 `[anim] running 2`，比静止时（`music_card_removed` 的 `[anim] running 1`）只多一条。
- 2026-10-19 · gui · signal · GUI-19 · 闪烁的深度（降到全透明）与曲线不在规则或 #72 中，本票自定；若觉得太重可改为降到半透明，只动 `service_gui_main_transport_flash()`，影响 `*_flash` 帧。可用 `/prototype` 比较。
- 2026-10-19 · gui · cite · GUI-4 · 本票基线变化：`music_card_removed` 的 `card_removed` 时间 Label 变为 `No SD card`；`no_card_play_stopped` 变为 `SD card not ready`，并从 10200 ms 挪到 10350 ms（原时刻落在闪烁中途）；新增 `card_removed_flash`、`no_card_play_flash` 两帧。`vinyl_rotation` 的 `cleared`、`cleared_hold` 变为 `No SD card`，`cleared_play_ignored` 拍在 `SD card not ready` 的闪烁中途。新增 `music_stop_reasons`（15 帧）与 `music_bad_file`（10 帧）。其余 11 个原有场景帧哈希不变。现为 15 场景/119 帧；`gui.regression.md` References 中的场景与帧数已过期（#84 Signal 已提）。
- 2026-10-19 · gui · signal · 无对应规则 · `vinyl_rotation` 的帧名 `cleared_play_ignored` 已与行为不符：快照驱动后无卡按播放并非被忽略，而是开启后按 NOT_READY 停止。改名会让基线键变化，本票未改。
- 2026-10-19 · harness · signal · 无对应规则 · 假播放后端的 `R` 键在停止态也再进入一次停止（停止次数 +1），用于逐个拍短语帧；真实后端只有 AUDIO_FAULT 锁存后的 TOGGLE/SELECT 会在停止态增加停止次数。模拟器 README“已知差异”已写明。

## 待办（writer branch drain 时执行）

- #86 验收要求删除 `docs/shape/gui.md` Open questions 中“过渡说明（2026-10-09）”：GUI-10、GUI-17、GUI-19、GUI-20 的目标态已全部由 #82–#86 实施。领域文件只在 writer branch 修改，故留到 drain 时删除（同 #84 inbox 第 3 条）。
- 唱盘 C8“代次 0 -> 非 0 也算身份变化”仍待用户决定（#83、#84 Signal），本票未改。
