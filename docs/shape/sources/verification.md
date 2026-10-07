# 验证入口与结果

规则见 [Harness](../harness.md)，路径选择以 [verification.psd1](../../../scripts/rules/verification.psd1) 为准；本页只提供命令和结果释义。

| 入口（仓库根执行） | 覆盖范围 |
| --- | --- |
| `./scripts/install-git-hooks.ps1` | 安装 pre-commit/commit-msg/pre-push |
| `./scripts/check-docs.ps1` | shape结构及自维护Markdown本地链接/锚点；支持 `-Snapshot Index`，不检查外网或代替内容审阅 |
| `./scripts/capture-verification.ps1 -Mode FULL`（或 `GUI`） | 运行既有入口，每次独立保存metadata.json、stdout.log、stderr.log；默认build/evidence，可用-OutputDirectory指定归档位置 |
| `./scripts/check_fast.ps1` | 含文档机械检查的工作树 FAST；pre-commit 使用 `-Snapshot Index` 检查暂存快照 |
| `./scripts/verify_changed.ps1` | FAST 和命中路径的 host 测试；pre-push 在待推送 SHA 的临时 worktree 执行 |
| `./scripts/verify.ps1` | FULL：FAST、Debug/Release 固件和全部 host 模块；兼容参数 `-Module` 不缩小范围 |
| `./Tools/gui_simulator/run-scenarios.ps1` | GUI 场景/帧哈希；不包含在 FAST/FULL，使用方法见 [模拟器](../../../Tools/gui_simulator/README.md) |
| `./scripts/sync-agent-config.ps1 -Check` | `.agents` 与生成包装一致性；去掉 `-Check` 生成包装 |

| 顶层 `HARNESS_STATUS` | 解释 |
| --- | --- |
| `PASS` | FAST/CHANGED 通过，未命中需板测路径 |
| `PASS_HOST_ONLY` | FULL 软件证据通过，未执行上板 |
| `NEEDS_HARDWARE_VALIDATION` | 软件检查通过，本次路径仍需板级证据 |
| `FAIL` | 检查、构建或测试失败，阻止提交/推送 |

Hook 实现分别见 [guard](../../../scripts/hooks/guard.ps1)、[check-edited](../../../scripts/hooks/check-edited.ps1)、[session-brief](../../../scripts/hooks/session-brief.ps1)。生成/分层/writer 边界直接查对应检查器，外部下载器证据范围见 [loader](../../../Tools/external_loader/README.md)。

证据提交时选用同一SHA且工作树状态可解释的运行记录，将metadata与完整日志归档到PR附件、证据评论或制品位置；build/evidence是本机收集目录，不能替代交付归档。shape规则的职责、来源与语义由未参与编写者按REVIEW审阅，自动检查只覆盖结构和本地引用。
