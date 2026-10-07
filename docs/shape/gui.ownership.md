# GUI · ownership

对象、设计变更与行为模块的职责。

[返回 GUI](gui.md)

### Rules

- **GUI-1** · provisional · 全部 Screen 在 `Service/gui/view/` 手写，不用图形化工具导出代码；boot/、main/*、theme/ 只经 `Service_GUI_ViewTypeDef` 句柄访问对象。_Why:_ 生成器与运行时补丁使同一界面出现多份事实源。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)
- **GUI-3** · provisional · 新增或改动界面前先更新本 area 的受影响设计约束；未决设计先列 Open questions 或 Proposed，再修改手写界面。_Why:_ 设计意图须先于实现明确，避免事后补记与重复长文。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)、[spec #15](https://github.com/immorcoding/mp3_project/issues/15)

### Rejected

- SquareLine 导出 GUI/ 作为事实源：主题、分页和事件仍需补丁；ADR-0007 已被 ADR-0016 取代。

### Signals

- 2026-10-07 · cite · GUI-3 · 唱盘旋转先在分支 inbox 记录设计，再修改 main/vinyl；view 对象树未改。
