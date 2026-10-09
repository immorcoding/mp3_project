# GUI · queue

窗口交接与播放列表行的稳定呈现。

[返回 GUI](gui.md)

### Rules

- **GUI-9** · provisional · Queue 只消费 READY 窗口副本，行数按 Length 且不超过容量，高亮须与当前 Catalog 代次一致；GUI 不解析路径或依赖 storage_listbuffer.h。 _Why:_ 窗口是跨任务快照，不能泄漏整表或混用旧代次。 _Source:_ [Queue 基线](../../Tools/gui_simulator/scenarios/queue.expected)、[GUI 任务](../../APP/tasks/gui/README.md)
- **GUI-16** · provisional · Queue 换窗回收 panel 并保留余像素，非首窗保留回滑行。 _Why:_ 换窗不能改变手指对应的像素位置或阻断反向滚动。 _Source:_ [Queue 基线](../../Tools/gui_simulator/scenarios/queue.expected)、[GUI 任务](../../APP/tasks/gui/README.md)
- **GUI-17** · provisional · Queue 点行先由 Service 即时高亮本窗再 post 下标，GUI 发 SELECT{已显示窗口代次, 位置}；GUI 给发送成功的命令编递增序号，快照回显序号落后时曲目与三态冻结在最后一份已追上的快照、位置照用；命令队列满则丢弃本次输入、记日志，并把即时高亮撤回到快照所示；当前曲高亮须回显已追上、快照代次非 0 且等于窗口代次；Queue 不随当前曲自动滚动。 _Why:_ 异步回显不能把刚选中的行或刚按下的状态覆盖回旧值，GUI 也不预测后端。 _Source:_ [Queue 基线](../../Tools/gui_simulator/scenarios/queue.expected)、[播放后端公开合同](https://github.com/immorcoding/mp3_project/issues/53)、[音乐页输入与列表](https://github.com/immorcoding/mp3_project/issues/66)
- **GUI-18** · provisional · Queue 行保持边宽和状态占位固定，只切换 Opa、调色板角色和 Long mode；封面归 Now Playing。 _Why:_ 状态变化不应挪动文字，列表行与播放封面各有展示职责。 _Source:_ [Queue 基线](../../Tools/gui_simulator/scenarios/queue.expected)、[GUI 任务](../../APP/tasks/gui/README.md)
