# Harness 脚本工作约定

继承根入口。修改前读 [harness](../docs/shape/harness.md)、[验证正文](../docs/shape/harness.verification.md)、目标脚本及对应 `*.Tests.ps1`；快照语义、兼容入口、路径映射与状态输出以验证正文为准。

- 嵌套入口抑制自己的 `HARNESS_STATUS=`，只由顶层入口输出一次。
- 保持 Windows PowerShell 5.1 兼容，不为规则读取或 Hook 引入额外依赖。
- 行为变化同步脚本自测；路径路由变化同步 `harness_helpers.Tests.ps1`。
- 除正向用例外，覆盖相关的索引/工作树不一致、推送提交/脏工作树不一致与失败状态场景。
- 验证后调用 `independent-verifier`；硬件分类变化再调用 `embedded-reviewer`。
