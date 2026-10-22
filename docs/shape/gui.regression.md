# GUI · regression

视觉变化的验收依据。

[返回 GUI](gui.md)

### Rules

- **GUI-4** · provisional · 模拟器帧哈希变化时先看截图、确认是有意的，才用 run-scenarios.ps1 -Update 重写基线，并在提交说明写明原因。_Why:_ 自动像素证据依赖基线不被随手覆盖。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)

### References

- [场景运行器](../../Tools/gui_simulator/README.md)：重现 `scenarios/*.expected` 中已批准的帧哈希与动画数断言并产出截图（数量以该目录为准）。

### Signals

- 2026-10-19 · friction · GUI-4 · `vinyl_rotation` 的帧名 `cleared_play_ignored` 已与行为不符（快照驱动后无卡按播放是开启后按 NOT_READY 停止，不是被忽略）；改名会改变基线键，#86 未改。
- 2026-10-22 · friction · GUI-4 · #106 的 grilling 判「现有基线帧不变」，实施后 12 场景 50 帧哈希仍变：LVGL v8 在动画链表为空时暂停动画定时器且不重置节拍，Lock 呼吸不再常驻后，唱盘角度、状态行闪烁透明度、翻页回弹位置的采样相位随之移动（在模拟器临时加常驻空动画，12 场景全部回到旧哈希；逐帧像素差只落在唱盘、状态行与 pager 左缘回弹条）。按本规则看图后 `-Update`。任何改变「是否有动画常驻」的改动都会移动其他动画的相位，判「基线不变」前须考虑。
