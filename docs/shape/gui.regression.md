# GUI · regression

视觉变化的验收依据。

[返回 GUI](gui.md)

### Rules

- **GUI-4** · provisional · 模拟器帧哈希变化时先看截图、确认是有意的，才用 run-scenarios.ps1 -Update 重写基线，并在提交说明写明原因。_Why:_ 自动像素证据依赖基线不被随手覆盖。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)

### References

- [场景运行器](../../Tools/gui_simulator/README.md)：重现已批准的八场景/54帧并产出截图。

### Signals

- 2026-10-07 · cite · GUI-4 · 新增 vinyl_rotation 场景，既有场景只更新已检查的四个旋转影响帧；最终八场景/54帧回归通过。
- 2026-10-09 · cite · GUI-4 · 0.6.0 音乐页计划把模拟器替身边界扩到 APP 音乐分区（编译真实 gui_music，假播放后端实现合同头，另配假 Storage 窗口）；基线分两步：先等价替换（旧场景哈希不变，变化帧逐张看图并写明原因），再接新行为、新增场景（[音乐页输入与列表](https://github.com/immorcoding/mp3_project/issues/66)）。
