# Tests 工作约定

本文件对 `Tests/` 全部子目录生效，并继承根 `AGENTS.md`。开始修改前，完整阅读 `Tests/README.md` 和目标测试目录 README。

## 本地规则

- Host 测试通过生产 Module 的公开 Interface 验证行为；Interface 是测试表面，不复制私有 Implementation。
- Fake/Mock 只实现真实 Seam 上已有的 Ops 或回调，不为测试向生产头增加专用入口。
- 每个测试 Module 保持独立 CMake 工程，只编译待测自维护源码和最小 fake；不得链接进目标固件。
- 测试必须在 Windows x86/x64 host compiler 下运行，不依赖 HAL、目标板寄存器、真实地址解引用或时间偶然性。
- 覆盖成功、边界、状态推进、失败注入和资源清理；测试名表达可观察行为。

## 完成条件

- 新增测试 Module 时同步 `Tests/README.md`、`scripts/test-host.ps1` 和 `scripts/rules/verification.psd1`。
- 确认 CHANGED 能从生产路径选中它，FULL 能执行它；新增路由同时补 `harness_helpers.Tests.ps1`。
- Host PASS 不得被描述为硬件 PASS。
