# GUI · regression

视觉变化的验收依据。

[返回 GUI](gui.md)

### Rules

- **GUI-4** · provisional · 模拟器帧哈希变化时先看截图、确认是有意的，才用 run-scenarios.ps1 -Update 重写基线，并在提交说明写明原因。_Why:_ 自动像素证据依赖基线不被随手覆盖。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)

### References

- [场景运行器](../../Tools/gui_simulator/README.md)：重现已批准的八场景/54帧并产出截图。

### Signals

- 2026-10-07 · cite · GUI-4 · 新增 vinyl_rotation 场景，既有场景只更新已检查的四个旋转影响帧；最终八场景/54帧回归通过。
