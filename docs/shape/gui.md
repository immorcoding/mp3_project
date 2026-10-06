# GUI

界面代码、主题与界面回归。布局、效果与设计目标分开按需读取；公开接口见 [Service/gui/README.md](../../Service/gui/README.md)。

Next id: GUI-5

## Pillars

- 一个界面只在一处描述：对象树在 `view/`，行为 Module 只拿句柄。
- 运行时效果（毛玻璃、分页、Queue 窗口、外观切换）优先于所见即所得。
- PC 上像素等价不等于板级验收。

## Open questions

- 何时升级 LVGL 9 并评估官方 XML 编辑器：ADR-0016 未采用，等需要升级 Vendor 时再定。

## interface

手写界面、主题所有权与设计文档。

### Rules

- **GUI-1** · provisional · 全部 Screen 在 `Service/gui/view/` 手写，不用图形化工具导出代码；boot/、main/*、theme/ 只经 `Service_GUI_ViewTypeDef` 句柄访问对象。_Why:_ SquareLine 的主题与组件模型迫使 Service 叠补丁，同一界面要看三处。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)
- **GUI-2** · provisional · 颜色只来自 `theme/` 按（颜色属性 × 调色板角色）持有的共享 `lv_style_t`，对象按角色引用；切外观时刷新这些 style 的颜色并调用 `lv_obj_report_style_change()`，不用 color filter，也不遍历对象改颜色 style；Screen 的背景透明度与壁纸由 `gui_service.c` 的编排设置。_Why:_ color filter 要靠占位 hex，且每次 `remove_style_all` 后都得整树重绑。_Source:_ ADR-0016 §3
- **GUI-3** · provisional · 新增或改动界面前先更新受影响的设计参考：布局与交互、效果生命周期或待实施设计，再修改手写界面。_Why:_ 手写界面的设计意图需要与实现同步，按主题维护能避免再次堆成流水。_Source:_ ADR-0016 后果；spec #15 收拢修订

### References

- [现行布局与交互](gui.design.md)：Lock、Main、Music 与 Queue（GUI-3）。
- [效果生命周期](gui.effects.md)：Boot 顺序、Canvas 独占与长期副本。
- [待实施设计](gui.planned.md)：Library/Reader/Settings 与缓存目标，均不表示已实现。

### Rejected

- 图形化生成器（SquareLine）导出 `GUI/` 作为界面事实源：主题、Tabview、Queue 范本与事件都要运行时补丁，删除于 PR #2。（[ADR-0007](../adr/0007-gui-runtime-and-squareline-boundary.md)，被 ADR-0016 取代）

## regression

模拟器像素基线与视觉验收。

### Rules

- **GUI-4** · provisional · 模拟器帧哈希变化时先看截图、确认是有意的，才用 `run-scenarios.ps1 -Update` 重写基线，并在提交说明写明原因。_Why:_ CHANGED/pre-push 命中 GUI 路径会自动跑场景回归，基线是界面等价的唯一自动证据，随手更新就失效。_Source:_ ADR-0016 §7

### References

- `Tools/gui_simulator/scenarios/*.expected`：已批准的 39 帧像素基线，Solid 外观 30 帧与 SquareLine 版逐像素一致（GUI-1、GUI-4）
