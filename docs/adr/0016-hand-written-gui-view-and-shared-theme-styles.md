# ADR-0016：手写 GUI 界面层与共享主题 style

- 状态：已接受。取代 [ADR-0007](0007-gui-runtime-and-squareline-boundary.md)。
- 日期：2026-10-01
- 相关实现说明：[../gui_ui_design.md](../shape/gui.design.md)、[../../Service/gui/README.md](../../Service/gui/README.md)、[../../Tools/gui_simulator/README.md](../../Tools/gui_simulator/README.md)

## 背景

ADR-0007 让 SquareLine Studio 的 `GUI/` 导出作为 UI 唯一事实源，GUI Service 只在运行时适配。实践中，产品 UI 的重心落在运行时效果上（Canvas 模糊与局部毛玻璃、循环分页、Queue 窗口、主题切换），SquareLine 只剩"摆初始布局"一项价值，却迫使 Service 叠加一组补丁：

- 五个占位 hex 加 `color_filter_dsc`，以及每次 `ui_init()`、`remove_style_all` 之后整树重绑；
- 强行改写 Tabview 内部 Content 的样式与滚动；
- 把 Queue 单行范本的构造代码抄进 for 循环，导出后再隐藏范本；
- 三键与进度条事件只能运行时补绑，Boot 交接靠生成代码按符号回调 Service；
- 运行时切到 Default 时，`Service_GUI_ThemeApply()` 在 Boot 被删除后返回 `SERVICE_NOT_READY`，但调色板已经换掉，画面只生效一半。

同一个界面要同时看 SquareLine 工程、`GUI/` 导出与 Service 覆盖三处。迁移前已建立 PC 模拟器（`Tools/gui_simulator/`），可以原样运行 `Service/gui` 并逐像素比对截图。

## 决定

1. **不再使用图形化生成器产出界面代码。** 全部 Screen 由 `Service/gui/view/`（GUI Service 私有 Module）手写创建；`GUI/` 与 `SquareLineProject/` 已从仓库删除（历史可从 git 取回），也不再受生成目录保护。设计稿可以用任何工具画，但不导出代码。
2. **view/ 只负责对象树与静态样式，并发布句柄**（`Service_GUI_ViewTypeDef`）。boot/、main/*、theme/ 等行为 Module 只通过句柄访问对象，不再依赖全局对象符号。屏幕自身的局部交互（Lock 上滑解锁与呼吸提示）可以留在 view/；跨屏时序（Boot → BootReveal → Lock）归 boot/。
3. **主题由共享颜色 style 承担。** theme/ 按（颜色属性 × 调色板角色）持有共享 `lv_style_t`，对象只按角色引用它们，Opa 等其他属性仍写在对象上。切外观只刷新这些 style 的颜色并调用 `lv_obj_report_style_change()`，不使用 color filter，也不扫对象改 style。
4. **`Service_GUI_ThemeApply()` 由 `gui_service.c` 编排**：先选调色板，再刷新共享 style 与各 Screen 的壁纸/Ground，最后由 main/background 设置 Music 毛玻璃或 Solid 薄层。启动序列未结束（Boot 仍存在）时返回 `SERVICE_BUSY`；首次切到 Default 时按需生成长期模糊壁纸，失败则回滚到原外观。
5. **以下约束沿用 ADR-0007，不变：** `Service/gui` 是 LVGL、Platform LCD/Touch 之间唯一的产品级运行时 Module；GUI Task 是除 LCD DMA 完成收尾外唯一调用 LVGL 的上下文；触摸 Platform 只发布原始坐标，方向与手势解释留在 GUI Service；对 APP 公开的 8 个 `Service_GUI_*` 函数签名不变。
6. **默认壁纸像素不随固件存放。** `view/gui_service_view_wallpaper_region.c` 在链接脚本的 SDRAM 资源区预留像素数组（沿用原输入段名，链接脚本不改），由 Resource Service 从资源包装入；描述符在 `view/gui_service_view_wallpaper.c`。
7. **界面等价性由模拟器场景回归守住。** `Tools/gui_simulator/run-scenarios.ps1` 以确定性虚拟时钟运行固定场景，比较每帧哈希。有意的视觉变化须看过截图后用 `-Update` 重写基线，并在提交说明写明原因。该回归只证明 PC 上的像素等价，不是板级验收。

## 未采用的方案

- **继续用 SquareLine，只修补丁。** 补丁的根因是生成器的主题与组件模型；只要 UI 继续往运行时效果方向演进，割裂只会加重。
- **升级 LVGL 9 并改用官方 XML 编辑器。** 它与运行时代码的共存方式更好，但要同时承担 Vendor 升级与 API 迁移；等需要升级 LVGL 时再评估。
- **SquareLine 只画草图、仍导出到 `GUI/` 供参考。** 导出物会与手写代码漂移，制造第二份"事实"。

## 后果

- 改界面只看 `Service/gui/view/` 一处；调布局要改代码再看模拟器，失去所见即所得拖拽。
- AGENTS.md 硬规则表与 ARC-2 删除 SquareLine 行；`check-generated-write`、Agent Hook 不再保护 `GUI/`、`SquareLineProject/`。
- 新增或改动界面前，仍需先更新 [gui.design.md](../shape/gui.design.md)；改动界面或 `Service/gui` 后跑场景回归，再上板（`hw:pending`）。
