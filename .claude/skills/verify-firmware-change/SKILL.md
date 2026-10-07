---
name: verify-firmware-change
description: Select and run FAST, CHANGED, FULL, and HARDWARE validation for this repository. Use when completing a non-trivial firmware change, checking commit or push readiness, changing harness rules/tests, refactoring, changing a public interface, or preparing a release; do not substitute it for diagnosing a specific failing test.
---

<!-- 由 scripts/sync-agent-config.ps1 从 .agents/skills/verify-firmware-change/SKILL.md 生成，勿手改。 -->

# 固件变更验证

验证入口和状态语义以 `docs/shape/sources/verification.md` 为准，不在 Skill 内复制路径映射。

## 选择层级

- 编辑循环或提交前：`./scripts/check_fast.ps1`；Git pre-commit 自动对索引快照运行它。
- 普通变更准备推送：`./scripts/verify_changed.ps1`；pre-push 会对实际推送提交运行它。
- 架构、公开 Interface、重构、Harness、里程碑或发布：`./scripts/verify_full.ps1`。兼容入口 `verify.ps1` 也固定执行 FULL。
- 输出 `NEEDS_HARDWARE_VALIDATION`，或需求本身涉及板级行为：完成声明前补充上板证据。

只有 `FAIL` 阻止 commit/push。`PASS_HOST_ONLY` 只证明完整软件验证，不表示硬件行为已验收。

完成前按 `docs/shape/harness.md` HAR-10 与 `docs/shape/REVIEW.md` 触发独立审阅：文档重构、规则迁移也在范围内；文档豁免仅限拼写、链接等显然无语义变化的小改，不能以“纯文档”跳过审阅。作者自查单列，不代替未参与编写者的独立首轮。问题修复与复审沿用 HAR-10。
