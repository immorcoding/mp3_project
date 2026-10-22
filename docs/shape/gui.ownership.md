# GUI · ownership

对象、设计变更与行为模块的职责，以及视觉变化的验收依据。

[返回 GUI](gui.md)

### Rules

- **GUI-1** · provisional · 全部 Screen 在 `Service/gui/view/` 手写，不用图形化工具导出代码；boot/、main/*、theme/ 只经 `Service_GUI_ViewTypeDef` 句柄访问对象。_Why:_ 生成器与运行时补丁使同一界面出现多份事实源。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)
- **GUI-3** · provisional · 新增或改动界面前先更新本 area 的受影响设计约束；未决设计先列 Open questions 或 Proposed，再修改手写界面。_Why:_ 设计意图须先于实现明确，避免事后补记与重复长文。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)、[spec #15](https://github.com/immorcoding/mp3_project/issues/15)
- **GUI-4** · provisional · 模拟器帧哈希变化时先看截图、确认是有意的，才用 run-scenarios.ps1 -Update 重写基线，并在提交说明写明原因。_Why:_ 自动像素证据依赖基线不被随手覆盖。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)

### References

- [场景运行器](../../Tools/gui_simulator/README.md)：重现 `scenarios/*.expected` 中已批准的帧哈希与动画数断言并产出截图（数量以该目录为准）。

### Signals

- 2026-10-07 · cite · GUI-3 · 唱盘旋转先在分支 inbox 记录设计，再修改 main/vinyl；view 对象树未改。
- 2026-10-18 · cite · GUI-1 · 新增 `time_label` 时 view 只创建 Label 并发布句柄、不写文本；初始 `0:00/--:--` 由 `main/transport` 在 prepare 时用 `Service_GUI_TimeApply` 的同一格式化函数写入，避免两处各写一份时间文本。
- 2026-10-19 · friction · GUI-4 · `vinyl_rotation` 的帧名 `cleared_play_ignored` 已与行为不符（快照驱动后无卡按播放是开启后按 NOT_READY 停止，不是被忽略）；改名会改变基线键，#86 未改。
- 2026-10-22 · friction · GUI-4 · #106 的 grilling 判「现有基线帧不变」，实施后 12 场景 50 帧哈希仍变：LVGL v8 在动画链表为空时暂停动画定时器且不重置节拍，Lock 呼吸不再常驻后，唱盘角度、状态行闪烁透明度、翻页回弹位置的采样相位随之移动（在模拟器临时加常驻空动画，12 场景全部回到旧哈希；逐帧像素差只落在唱盘、状态行与 pager 左缘回弹条）。按本规则看图后 `-Update`。任何改变「是否有动画常驻」的改动都会移动其他动画的相位，判「基线不变」前须考虑。

### Rejected

- SquareLine 导出 GUI/ 作为事实源：主题、分页和事件仍需补丁；ADR-0007 已被 ADR-0016 取代。
