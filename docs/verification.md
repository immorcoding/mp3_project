# 分层验证与 Agent Harness

本文是工程验证入口、结果状态和审校触发策略的唯一说明。模块职责仍由 `CONTEXT.md`、架构文档和各 Module README 维护；确定性路径映射只维护在 `scripts/rules/verification.psd1`。

## 四级验证

| 层级 | 入口 | 自动时机 | 验证范围 |
| --- | --- | --- | --- |
| FAST | `./scripts/check_fast.ps1` | pre-commit | Harness 自测、分层 include、生成目录写保护 |
| CHANGED | `./scripts/verify_changed.ps1` | pre-push | FAST + 由变更路径选出的 host fake/mock 测试 |
| FULL | `./scripts/verify_full.ps1` | 手动 | FAST + 固件 Debug/Release + 全部 host fake/mock 测试 |
| HARDWARE | 对应模块的板级清单 | 手动 | 中断、DMA、Cache、时序、掉电和真实外设行为 |

兼容入口 `./scripts/verify.ps1` 固定转发到 FULL。它仍接受旧 `-Module` 参数，但会忽略该参数，避免名为“完整验收”的命令被降级；需要选择模块时使用 CHANGED。

## 快照语义

pre-commit 必须验证即将提交的 Git 索引，而不是当前工作树。FAST 的 Hook 调用为：

```powershell
./scripts/check_fast.ps1 -Snapshot Index
```

因此，文件暂存后又在工作树修正，暂存版本里的跨层 include 或受保护生成文件变化仍会失败。手动运行 FAST 默认检查工作树。

pre-push 从 Git 提供的 ref 更新读取提交范围。每个待推送 SHA 都在系统临时目录创建 detached worktree，再运行快检和受影响主机测试；当前工作树的未提交内容不会污染推送证据。临时 worktree 只在系统临时目录内创建和清理。

## 影响映射

`scripts/rules/verification.psd1` 使用 Windows PowerShell 5.1 原生可读的数据格式，集中维护：

- 所有 host 测试模块；
- 生产/测试路径到 `external_loader`、`flash_ftl`、`w25qxx`、`resource_pack` 的多对多映射；
- 纯文档和 Agent 配置的跳过规则；
- Harness、构建系统等强制全部 host 测试的规则；
- MCU/板级敏感路径。

一条路径可命中多个模块。例如 `Components/flash_ftl/` 同时进入 `flash_ftl` 和 `w25qxx`，因为两个测试工程都编译 FTL 实现。未识别的非文档路径必须记录证据并回退全部 host 测试，禁止以“零测试”通过。

`external_loader` host 模块只验证地址窗口几何；生产 Loader 变化还必须按目录 README 交叉构建 `.stldr`，并保留 CubeProgrammer 真机验证状态。

修改映射时同步修改 `scripts/harness_helpers.Tests.ps1`，并先运行 FAST。不要在 Hook、Skill 或 Agent 配置中复制路径表。

## 状态

每个顶层入口输出一个机器可读的 `HARNESS_STATUS=`：

| 状态 | 含义 | 是否阻止 Hook |
| --- | --- | --- |
| `PASS` | 当前 FAST/CHANGED 层级完成，未发现必须上板的路径 | 否 |
| `PASS_HOST_ONLY` | FULL 软件证据完整，但命令本身没有执行上板 | 否 |
| `NEEDS_HARDWARE_VALIDATION` | 软件验证通过，变更仍需要板级证据 | 否；但不得据此声称功能完成 |
| `FAIL` | 脚本、构建、规则或测试失败 | 是 |

只有 `FAIL` 阻止 commit/push。允许推送 `NEEDS_HARDWARE_VALIDATION` 是为了保留协作节奏，不表示板级验收已经完成。

## Hook 安装

克隆后运行一次：

```powershell
./scripts/install-git-hooks.ps1
```

它只设置本仓库的 `core.hooksPath=.githooks`。`pre-commit` 使用索引快照跑 FAST；`pre-push` 对实际推送提交跑 CHANGED。现有 `ALLOW_GENERATED_UPDATE` 规则保持不变，助手禁止设置该变量。

## Agent 路由与独立审校

根 `AGENTS.md` 保存全局工作合同；主要自维护层、host 测试、Harness 脚本和 external loader 的子 `AGENTS.md` 只补充所在目录的本地 Seam、必读指针与完成条件。进入目标路径时读取最近的子文件，不把 Module README、领域词典或本页的路径映射复制进去。生成目录不放子 `AGENTS.md`。

项目级 Skill 位于 `.agents/skills/`：

- `mp3-firmware-change`：根据需求和实际路径读取相关模块文档，不复制领域词典；
- `verify-firmware-change`：选择 FAST、CHANGED、FULL、HARDWARE；
- `review-embedded-change`：处理 DMA、Cache、ISR、RTOS、HAL、Platform、链接段和硬件生命周期风险。

项目级只读审校者位于 `.codex/agents/`：

- `independent-verifier`：非 trivial 功能、修复、重构、Harness 修改完成后自动调用；纯文档、拼写和显然无行为影响的小配置可跳过；
- `embedded-reviewer`：命中硬件敏感行为时自动调用；用户可随时明确强制两者之一。

两个审校者不进入 Git Hook，也不替代确定性脚本。两者同时需要时并行启动，独立形成第一遍结论；修复审校问题后重跑相应脚本，只有修复改变了原审校依据时才重新调用。

不采用“一 Module 一 Skill”。只有某 Module 出现高风险、重复且独特的多步骤操作流程时，才把操作手册晋升为独立 Skill；职责、Interface、不变量和术语继续留在正式文档中。
