# Architecture

分层、依赖方向与生成器边界；正文在 ADR 与架构文档，本文件只放不可违背的入口规则。

## Pillars

- 功能/抽象所有权、编译期 `#include`、运行时请求/回调是三张图，不互相推导。
- 生成器是唯一事实来源。
- HAL 的不合适在 Adapter 层吸收，不外泄。

## Rules

- **ARC-1** · settled · 分层与装配以 `docs/architecture_standard.md` 为准；`#include` 方向由 `scripts/check-layer-includes.ps1` 强制。_Source:_ [ADR-0002](../adr/0002-layering-and-interface-ownership.md)
- **ARC-2** · settled · CubeMX（`io_sheet.ioc` 产物）、Vendor（HAL/CMSIS/中间件）产物不手改；参数改源工程后重新导出，HAL 缺陷在 `Adapters/stm32_hal/` 绕过。界面不再由生成器产出，`Service/gui/view/` 是手写自维护代码。_Why:_ 重新导出会覆盖手改。_Source:_ [ADR-0016](../adr/0016-hand-written-gui-view-and-shared-theme-styles.md)
- **ARC-3** · settled · CubeMX 文件的 `USER CODE` 区只放对自维护入口的调用或转发，不放产品逻辑。_Source:_ `architecture_standard.md` §3.1
- **ARC-4** · settled · 会长期约束 Module 所有权、Interface 接缝、并发模型、资源生命周期的决定写 ADR。_Source:_ [domain.md](../agents/domain.md)
