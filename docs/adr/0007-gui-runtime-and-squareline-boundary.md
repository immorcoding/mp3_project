# ADR-0007：GUI 运行时所有权与 SquareLine 生成边界

- 状态：已被 [ADR-0016](0016-hand-written-gui-view-and-shared-theme-styles.md) 取代（2026-10-01）。第 1、5、6 条（SquareLine 事实源、生成事件交接点、占位色过滤器）废止；第 2–4 条的运行时所有权、单 LVGL 上下文与触摸原始坐标约束由 ADR-0016 沿用。下文保留作历史记录。
- 日期：2026-08-29（2026-09-06 增补配色所有权；2026-09-07 增补输入单槽、music 分区与假进度）
- 相关实现说明：[../gui_ui_design.md](../gui_ui_design.md)、[../../Service/gui/README.md](../../Service/gui/README.md)

## 背景

SquareLine Studio 生成的 `GUI/` 目录会在重新导出时覆盖；与此同时 LVGL、LCD DMA、触摸坐标、Canvas 资源和启动动画需要运行时行为。若直接修改生成代码，或者让多个 Task 调用 LVGL，都会破坏可再生 UI 与线程安全。

## 决定

1. `GUI/` 与 SquareLine 工程是 UI 原型唯一事实来源；助手和手写运行时代码均不得修改生成源、生成 CMake 或资源清单。
2. `Service/gui` 是 LVGL、Platform LCD、Platform Touch 与 SquareLine 对象之间的唯一产品级运行时 Module；对 APP 公开 `Service_GUI_Init()`、`Service_GUI_Process()`、`Service_GUI_QueueScrollLead()`、`Service_GUI_ConsumeInput()`、`Service_GUI_TransportApply()`、`Service_GUI_ProgressApply()`、`Service_GUI_QueueApply()` 与 `Service_GUI_ThemeApply()`。Queue 窗口协议留在 GUI Task 的 `music/` 分区：按滚动改 `Index` 后 `request` / 消费 READY / 写回 IDLE；点击由 Service `post` 命令、GUI Task `ConsumeInput` 后由 `gui_music_step` 改游标、playing 或假进度。不让 GUI Service 包含 `storage_listbuffer.h` 或 `storage_playback_cursor.h`。
3. GUI Task 是除 LCD DMA 完成必要收尾外唯一调用 LVGL Interface 的上下文；其他 Task 与 ISR 不创建对象、不处理布局或读取触摸。
4. 触摸 Platform 只发布原始坐标和按下状态，方向、镜像和 UI 手势解释保留在 GUI Service。
5. Boot、Canvas、Main Pager、Main Queue、Main Transport 和 Main Background 只作为 GUI Service 私有 Module。SquareLine 事件只允许调用窄的内部交接点，不成为公开 Service Interface。Now Playing 三键的 `CLICKED` 与进度条的 `RELEASED` 由 Transport 在运行时绑定，不得写进 SquareLine。
6. 配色不属于 SquareLine 主题系统。导出颜色是五个占位 hex；GUI Service 持有调色板与当前索引，用 LVGL `color_filter_dsc` 映射，不调用 `ui_theme_set()`。不得手改 `GUI/ui.c`：`ui_init()` 会 `lv_theme_basic_init` 覆盖 display theme，因此过滤器必须在 `ui_init()` 之后重新挂上，并对已创建对象整树再绑。Opa、按下态留在对象上。壁纸显隐与 Music 毛玻璃是切主题的副作用，不是独立状态机，也不与电量/时间混装。`Service_GUI_ThemeApply()` 仅由 GUI Task 调用。

## 后果

- 用户能始终在 SquareLine 中修改视觉并重新导出，运行时代码只做适配；
- Canvas/分页等复杂效果不会污染生成代码或泄漏给 APP；
- GUI 设计每次推进需先同步 `docs/gui_ui_design.md`，运行时调整则在 `Service/gui` 中实现并记录资源生命周期。
