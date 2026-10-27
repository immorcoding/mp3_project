# 验证入口与结果

规则见 [Harness](../harness.md)，路径选择以 [verification.psd1](../../../scripts/rules/verification.psd1) 为准；本页只提供命令和结果释义。

| 入口（仓库根执行） | 覆盖范围 |
| --- | --- |
| `./scripts/install-git-hooks.ps1` | 安装 pre-commit/commit-msg/pre-push |
| `./scripts/check-docs.ps1` | shape结构、分支 inbox 行格式及自维护Markdown本地链接/锚点；支持 `-Snapshot Index`，不检查外网或代替内容审阅 |
| `./scripts/check-ps1-encoding.ps1` | 含非 ASCII 字节的 `.ps1` 须带 UTF-8 BOM（Windows PowerShell 5.1 否则按 ANSI 解析，中文注释与弯引号会让解析在别处报错）；FAST 运行，跳过 `build/`、`.claude/` |
| `./scripts/capture-verification.ps1 -Mode FULL`（或 `GUI`） | 运行既有入口，每次独立保存metadata.json、stdout.log、stderr.log；默认build/evidence，可用-OutputDirectory指定归档位置 |
| `./scripts/check-doxygen.ps1` | 自维护 C/H 中新增或修改的函数定义须有紧邻中文 Doxygen（STY-2 机械项）；支持 `-Snapshot Index`、`-BaseRef/-HeadRef`，`-Report` 只打印全仓库存量计数、不失败；盲区见脚本头注释 |
| `./scripts/check-test-paths.ps1` | 核对 `verification.psd1` 中每个主机模块的 `PathPatterns` 覆盖其测试工程实际编译的全部仓库头文件与源文件（需 cmake、ninja、主机 GCC）；盲区见脚本头注释 |
| `./scripts/check_fast.ps1` | 含文档机械检查、Doxygen 与主机测试路径覆盖的工作树 FAST；pre-commit 使用 `-Snapshot Index` 检查暂存快照 |
| `./scripts/verify_changed.ps1` | FAST 和命中路径的 host 测试；pre-push 在待推送 SHA 的临时 worktree 执行 |
| `./scripts/verify.ps1` | FULL：FAST、Debug/Release 固件、全部 host 模块和 GUI 模拟器场景；兼容参数 `-Module` 不缩小范围 |
| `./Tools/gui_simulator/run-scenarios.ps1` | GUI 场景/帧哈希；FULL 与命中 GUI 路径的 CHANGED 都会运行，单独运行用于看图和 `-Update` 重写基线（GUI-4），使用方法见 [模拟器](../../../Tools/gui_simulator/README.md) |
| `./scripts/sync-agent-config.ps1 -Check` | `.agents` 与生成包装一致性；去掉 `-Check` 生成包装 |

| 顶层 `HARNESS_STATUS` | 解释 |
| --- | --- |
| `PASS` | FAST/CHANGED 通过，未命中需板测路径 |
| `PASS_HOST_ONLY` | FULL 软件证据通过，未执行上板 |
| `NEEDS_HARDWARE_VALIDATION` | 软件检查通过，本次路径仍需板级证据 |
| `FAIL` | 检查、构建或测试失败，阻止提交/推送 |

Hook 成功时只打印汇总与 `HARNESS_STATUS`，失败时输出完整细节（`Invoke-HarnessQuietStep` 缓冲子进程输出）。以 `2>&1` 合并 stderr 调用与重定向到文件等价，由 `capture-verification.MergedStderr.Tests.ps1`（FAST）覆盖，无需 `cmd /c` 变通；构建入口经 `Invoke-ExternalCommand` 调用原生命令，只以退出码判定成败，同一行为由 `build_helpers.Tests.ps1`（FAST）覆盖。

Hook 实现分别见 [guard](../../../scripts/hooks/guard.ps1)、[check-edited](../../../scripts/hooks/check-edited.ps1)、[session-brief](../../../scripts/hooks/session-brief.ps1)。生成/分层/writer 边界直接查对应检查器，外部下载器证据范围见 [loader](../../../Tools/external_loader/README.md)。

证据提交时选用同一SHA且工作树状态可解释的运行记录，将metadata与完整日志归档到PR附件、证据评论或制品位置；build/evidence是本机收集目录，不能替代交付归档。shape规则的职责、来源与语义由未参与编写者按根 AGENTS「完成前」审阅，规范来源见 [CODING_STANDARDS](../../../CODING_STANDARDS.md)，自动检查只覆盖结构和本地引用。
