# Harness 脚本工作约定

本文件对 `scripts/` 全部子目录生效，并继承根 `AGENTS.md`。修改前完整阅读 `../docs/verification.md`、目标脚本及其对应 `*.Tests.ps1`。

## 本地规则

- 保留现有 Git Hooks、host fake 测试和 `verify.ps1` 兼容入口；所有验证都归入 FAST → CHANGED → FULL → HARDWARE。
- Git 索引、当前工作树和待推送 commit 是三种不同快照；不得用一种快照的脚本、规则或路径污染另一种证据。
- 路径到 host 测试、纯文档跳过、全量回退和硬件敏感性只维护在 `rules/verification.psd1`。
- 顶层入口只输出一个 `HARNESS_STATUS=`；嵌套入口必须抑制自己的状态行。
- Harness 保持 Windows PowerShell 5.1 兼容，不为读取规则或运行 Hook 引入额外依赖。

## 完成条件

- 行为变化同步相应脚本自测；路径路由变化同步 `harness_helpers.Tests.ps1`。
- 正向测试之外，必须覆盖索引/工作树不一致、推送提交/脏工作树不一致或失败状态等相关负向场景。
- 运行 FAST 和 FULL，并调用只读 `independent-verifier`；硬件分类变化再调用 `embedded-reviewer`。
