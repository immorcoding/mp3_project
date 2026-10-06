---
name: verify-firmware-change
description: Select and run FAST, CHANGED, FULL, and HARDWARE validation for this repository. Use when completing a non-trivial firmware change, checking commit or push readiness, changing harness rules/tests, refactoring, changing a public interface, or preparing a release; do not substitute it for diagnosing a specific failing test.
---

# 固件变更验证

验证入口和状态语义以 `docs/shape/sources/verification.md` 为准，不在 Skill 内复制路径映射。

## 选择层级

- 编辑循环或提交前：`./scripts/check_fast.ps1`；Git pre-commit 自动对索引快照运行它。
- 普通变更准备推送：`./scripts/verify_changed.ps1`；pre-push 会对实际推送提交运行它。
- 架构、公开 Interface、重构、Harness、里程碑或发布：`./scripts/verify_full.ps1`。兼容入口 `verify.ps1` 也固定执行 FULL。
- 输出 `NEEDS_HARDWARE_VALIDATION`，或需求本身涉及板级行为：完成声明前补充上板证据。

只有 `FAIL` 阻止 commit/push。`PASS_HOST_ONLY` 只证明完整软件验证，不表示硬件行为已验收。

非 trivial 软件变更完成后调用只读 `independent-verifier`。如果修改只是拼写、纯文档或显然无行为影响的小配置，可不调用；用户明确要求时始终调用。审校指出问题后先修复并重跑确定性脚本，只有修复改变了审校依据时才重新调用。
