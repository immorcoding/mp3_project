# GUI · ownership

对象、设计变更与行为模块的职责，以及视觉变化的验收依据。

[返回 GUI](gui.md)

### Rules

- **GUI-1** · provisional · 全部 Screen 在 `Service/gui/view/` 手写，不用图形化工具导出代码；boot/、main/*、theme/ 只经 `Service_GUI_ViewTypeDef` 句柄访问对象。_Why:_ 生成器与运行时补丁使同一界面出现多份事实源。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)
- **GUI-3** · provisional · 新增或改动界面前先更新本 area 的受影响设计约束；未决设计先列 Open questions 或 Proposed，再修改手写界面。_Why:_ 设计意图须先于实现明确，避免事后补记与重复长文。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)、[spec #15](https://github.com/immorcoding/mp3_project/issues/15)
- **GUI-4** · provisional · 模拟器帧哈希变化时先看截图、确认是有意的，才用 run-scenarios.ps1 -Update 重写基线，并在提交说明写明原因；改变「是否有动画常驻」的改动会移动其他动画的采样相位（LVGL 在动画链表为空时暂停动画定时器），判「基线不变」前先跑 run-scenarios.ps1 核对。_Why:_ 自动像素证据依赖基线不被随手覆盖；#106 判「基线不变」后仍有 12 场景 50 帧因相位移动而变。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)

### References

- [场景运行器](../../Tools/gui_simulator/README.md)：重现 `scenarios/*.expected` 中已批准的帧哈希与动画数断言并产出截图（数量以该目录为准）。

### Signals

- 2026-10-07 · cite · GUI-3 · 唱盘旋转先在分支 inbox 记录设计，再修改 main/vinyl；view 对象树未改。
- 2026-10-18 · cite · GUI-1 · 新增 `time_label` 时 view 只创建 Label 并发布句柄、不写文本；初始 `0:00/--:--` 由 `main/transport` 在 prepare 时用 `Service_GUI_TimeApply` 的同一格式化函数写入，避免两处各写一份时间文本。
- 2026-10-26 · friction · GUI-4 · 模拟器约每 20 ms 刷新一帧，假 Storage 默认晚一圈（5 ms）应答，切歌空白与开播空白通常拍不到，帧哈希证明不了“有空白”或“没有空白”；#120 加 `--name-delay` 把应答拉长到超过一个刷新周期再拍。可考虑写明：短于一个刷新周期的瞬态先用替身延迟放大再截图。
- 2026-10-26 · cite · GUI-4 · #121 曲名换字体后 20 个场景约 130 帧哈希变化；用 `--font-unavailable`（回退 Montserrat，与改动前同字体）重跑既有场景，17 个逐帧相同，余下 4 帧可由曲库扩到 16 首解释，于是看图只需看曲名区。
- 2026-10-27 · cite · GUI-4 · #140 窗口行改为 {曲目显示名, 歌手} 后有 8 个既有场景 18 帧变化。核对办法：在 62f4ae1 的独立 worktree 渲染旧截图，逐帧左右拼图比对；全部差异只在第 5 行（标题 + 歌手）与第 7 行（标题无歌手）两行，其余行文字因原 10 首无目录与扩展名而不变。假标签放在第 7 首而不是第 2、3 首，是因为后者会进入唱盘页场景、连带改动曲名基线。

### Rejected

- SquareLine 导出 GUI/ 作为事实源：主题、分页和事件仍需补丁；ADR-0007 已被 ADR-0016 取代。
