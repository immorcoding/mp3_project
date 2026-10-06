---
name: mp3-firmware-change
description: Route implementation, fixes, and refactors in this STM32H743 MP3 firmware to the affected Module documentation and verification without duplicating the domain dictionary. Use for changes under APP, Service, Platform, Components, Adapters, FATFS, FreeRTOS configuration, or firmware build integration; do not use for unrelated repository prose edits.
---

<!-- 由 scripts/sync-agent-config.ps1 从 .agents/skills/mp3-firmware-change/SKILL.md 生成，勿手改。 -->

# MP3 固件变更路由

任务来自 GitHub issue 时先读该 issue 及评论；再读目标路径上最近的子 `AGENTS.md`，沿 `docs/shape/ROUTES.md` 读取相关领域规则、Module README 和技术正文，术语按条查 `GLOSSARY.md`；不要把这些事实复制进本 Skill 或子 `AGENTS.md`。

## 路由

1. 区分功能/抽象所有权、编译期 include 与运行时请求/事件三种关系。跨层或公开 Interface 变化必须读取 `docs/shape/architecture.md` 与 `docs/architecture_standard.md`。
2. 用 `scripts/rules/verification.psd1` 判断已有主机测试覆盖和硬件敏感路径。该文件是确定性路径映射的唯一来源；发现未知生产路径时，不得解释成“无需测试”。
3. 实现期间运行最窄的相关测试；完成非 trivial 变更时使用 `verify-firmware-change` 的层级规则。
4. 命中硬件敏感路径时同时使用 `review-embedded-change`。Skill 只触发审阅，不替代上板验证。

生成/Vendor 保护与改动范围沿用根入口；GUI 变化先读 `docs/shape/gui.md` 并按 GUI-3 更新设计，完成后跑 `Tools/gui_simulator/run-scenarios.ps1`。新增 Module 按 ARC-13 确认职责、Interface 和真实实现。
