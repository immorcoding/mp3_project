# GUI · runtime

GUI Task 的循环节奏与让出。

[返回 GUI](gui.md)

### Rules

- **GUI-28** · provisional · GUI Task 每圈结束后睡到下一个 LVGL 定时器到期（扣除本圈耗时），夹在 1 ms 与刷新周期之间；有待取走的输入时只睡下限；新结果到达不提前唤醒。_Why:_ 低优先级任务靠 GUI 让出 CPU；屏幕只按刷新周期更新，提前唤醒换不来更早的显示。_Source:_ [GUI 没活就睡的规则与接口](https://github.com/immorcoding/mp3_project/issues/190)、[ADR-0023](../adr/0023-task-split-and-app-contract.md)
