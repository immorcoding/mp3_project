# Architecture

分层、依赖方向与生成器边界；正文在 ADR 与架构文档，本文件只放不可违背的入口规则。

Next id: ARC-5

## Pillars

- 功能/抽象所有权、编译期 `#include`、运行时请求/回调是三张图，不互相推导。
- 生成器是唯一事实来源。
- HAL 的不合适在 Adapter 层吸收，不外泄。

## Rules

- **ARC-1** · settled · 分层与装配以 `docs/architecture_standard.md` 为准。_Source:_ [ADR-0002](../adr/0002-layering-and-interface-ownership.md) _Check:_ `scripts/check-layer-includes.ps1`（FAST、PostToolUse）强制 `#include` 方向。
- **ARC-2** · settled · CubeMX（`io_sheet.ioc` 产物）、Vendor（HAL/CMSIS/中间件）产物不手改；参数改源工程后重新导出，HAL 缺陷在 `Adapters/stm32_hal/` 绕过。界面不是生成产物（GUI-1）。_Why:_ 重新导出会覆盖手改。_Source:_ `architecture_standard.md` §3.1 _Check:_ Agent Hook 拦截对全部生成/Vendor 产物的写入；Git 层 `check-generated-write.ps1`（FAST/CHANGED）只覆盖 Vendor 目录与 `cmake/stm32cubemx/`。
- **ARC-3** · settled · CubeMX 文件的 `USER CODE` 区只放对自维护入口的调用或转发，不放产品逻辑。_Source:_ `architecture_standard.md` §3.1 _Check:_ Agent Hook 对含 `USER CODE` 的文件走 ask（Codex deny），由用户确认。
- **ARC-4** · settled · 会长期约束 Module 所有权、Interface 接缝、并发模型、资源生命周期的决定写 ADR。_Source:_ [domain.md](../agents/domain.md) _Check:_ 人工审阅，按 domain.md 的 ADR 范围判断。
