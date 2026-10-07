# Tests 工作约定

继承根入口。修改前读 [主机回归说明](README.md)、目标测试 README 与 [验证正文](../docs/shape/sources/verification.md)；独立构建边界见 [architecture](../docs/shape/architecture.md) ARC-14。

- 经生产公开接口验证可观察行为；Fake/Mock 只实现既有 Ops/回调，不向生产头增加测试专用入口。
- 测试运行在 Windows x86/x64 host，不依赖 HAL、真实寄存器/地址或时间偶然性；覆盖成功、边界、状态推进、失败注入与资源清理，测试名表达行为。
- 新增测试模块同步本目录 README、`scripts/test-host.ps1` 与 `scripts/rules/verification.psd1`。
- 确认 CHANGED 能从生产路径选中测试、FULL 能执行；新增路由同步补 `scripts/harness_helpers.Tests.ps1`。证据状态沿用根入口。
