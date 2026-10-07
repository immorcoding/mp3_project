# GUI · queue

窗口交接与播放列表行的稳定呈现。

[返回 GUI](gui.md)

### Rules

- **GUI-9** · provisional · Queue 只消费 READY 窗口副本，行数按 Length 且不超过容量，高亮须与当前 Catalog 代次一致；GUI 不解析路径或依赖 storage_listbuffer.h。 _Why:_ 窗口是跨任务快照，不能泄漏整表或混用旧代次。 _Source:_ [Queue 基线](../../Tools/gui_simulator/scenarios/queue.expected)、[GUI 任务](../../APP/tasks/gui/README.md)
- **GUI-16** · provisional · Queue 换窗回收 panel 并保留余像素，非首窗保留回滑行。 _Why:_ 换窗不能改变手指对应的像素位置或阻断反向滚动。 _Source:_ [Queue 基线](../../Tools/gui_simulator/scenarios/queue.expected)、[GUI 任务](../../APP/tasks/gui/README.md)
- **GUI-17** · provisional · Queue 点击先更新本窗，再 post 下标；cursor 更新后才 Apply。 _Why:_ 异步回传不能把刚选中的行覆盖回旧游标。 _Source:_ [Queue 基线](../../Tools/gui_simulator/scenarios/queue.expected)、[GUI 任务](../../APP/tasks/gui/README.md)
- **GUI-18** · provisional · Queue 行保持边宽和状态占位固定，只切换 Opa、调色板角色和 Long mode；封面归 Now Playing。 _Why:_ 状态变化不应挪动文字，列表行与播放封面各有展示职责。 _Source:_ [Queue 基线](../../Tools/gui_simulator/scenarios/queue.expected)、[GUI 任务](../../APP/tasks/gui/README.md)
