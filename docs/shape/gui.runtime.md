# GUI · runtime

GUI Task 的循环节奏与让出。

[返回 GUI](gui.md)

### Rules

- **GUI-28** · provisional · GUI Task 每圈结束后睡到下一个 LVGL 定时器到期（扣除本圈耗时），夹在 1 ms 与刷新周期之间；有待取走的输入时只睡下限；新结果到达不提前唤醒。_Why:_ 同级（前台、Log、Monitor，时间片轮转）和低优先级（后台）任务靠 GUI 让出 CPU；屏幕只按刷新周期更新，提前唤醒换不来更早的显示。_Source:_ [GUI 没活就睡的规则与接口](https://github.com/immorcoding/mp3_project/issues/190)、[ADR-0023](../adr/0023-task-split-and-app-contract.md)

### Proposed

- 2026-11-16 · provisional · GUI-28 补一句限定：睡眠时长只扣 `Service_GUI_Process()` 内部的本圈耗时，Process 之前的 `gui_step` 耗时不扣，所以帧间隔是刷新周期加这段耗时（通常不足一个 tick）；`vTaskDelay(1)` 只睡到下一个 tick，实际 0 到 1 ms；上下限是 `SERVICE_GUI_SLEEP_MIN_MS`、`SERVICE_GUI_SLEEP_MAX_MS`（1、16 ms）。_Why:_ “扣除本圈耗时”的边界不写清，读者会以为连 `gui_step` 也扣了。第 7 次板测 R1.2：GUI 最长帧 Debug 96.5 ms（开机）、Release 5.2 ms。（queued: 无人值守 drain，#205 inbox）
