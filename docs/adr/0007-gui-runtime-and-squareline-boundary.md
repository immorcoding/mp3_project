# ADR-0007：GUI 运行时所有权与 SquareLine 生成边界

- 状态：已接受（按现有代码追溯记录）
- 日期：2026-08-29
- 相关实现说明：[../gui_ui_design.md](../gui_ui_design.md)、[../../Service/gui/README.md](../../Service/gui/README.md)

## 背景

SquareLine Studio 生成的 `GUI/` 目录会在重新导出时覆盖；与此同时 LVGL、LCD DMA、触摸坐标、Canvas 资源和启动动画需要运行时行为。若直接修改生成代码，或者让多个 Task 调用 LVGL，都会破坏可再生 UI 与线程安全。

## 决定

1. `GUI/` 与 SquareLine 工程是 UI 原型唯一事实来源；助手和手写运行时代码均不得修改生成源、生成 CMake 或资源清单。
2. `Service/gui` 是 LVGL、Platform LCD、Platform Touch 与 SquareLine 对象之间的唯一产品级运行时 Module；对 APP 只公开 `Service_GUI_Init()` 与 `Service_GUI_Process()`。
3. GUI Task 是除 LCD DMA 完成必要收尾外唯一调用 LVGL Interface 的上下文；其他 Task 与 ISR 不创建对象、不处理布局或读取触摸。
4. 触摸 Platform 只发布原始坐标和按下状态，方向、镜像和 UI 手势解释保留在 GUI Service。
5. Boot、Canvas、Main Pager 和 Main Background 只作为 GUI Service 私有 Module。SquareLine 事件只允许调用窄的内部交接点，不成为公开 Service Interface。

## 后果

- 用户能始终在 SquareLine 中修改视觉并重新导出，运行时代码只做适配；
- Canvas/分页等复杂效果不会污染生成代码或泄漏给 APP；
- GUI 设计每次推进需先同步 `docs/gui_ui_design.md`，运行时调整则在 `Service/gui` 中实现并记录资源生命周期。
