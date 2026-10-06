# Harness · 验证参考

本页说明验证命令、快照与结果的解释；约束见 [Harness](harness.md)，实际路径映射唯一来源为 [`verification.psd1`](../../scripts/rules/verification.psd1)。

## 层级与入口

| 层级 | 命令（仓库根执行） | 证据 |
| --- | --- | --- |
| FAST | `./scripts/check_fast.ps1` | Harness/Hook 自测、include 与生成写保护、包装一致性；pre-commit 检查暂存索引及 shape writer |
| CHANGED | `./scripts/verify_changed.ps1` | FAST 与路径命中的 host 测试；pre-push 验证实际推送快照，GUI 路径另跑场景 |
| FULL | `./scripts/verify.ps1` 或 `./scripts/verify_full.ps1` | FAST、Debug/Release 固件、全部 host 模块 |
| HARDWARE | 对应领域/模块的板级清单 | 真实中断、DMA、Cache、时序、掉电与器件行为 |

兼容入口 `verify.ps1` 始终为 FULL；旧 `-Module` 参数被忽略，局部验证使用 CHANGED。**FAST 和 FULL 均不包含 GUI 场景**：GUI 修改另跑 `./Tools/gui_simulator/run-scenarios.ps1`，依 GUI-4 处理有意像素变化。

GUI 回归以确定性时钟运行真实 GUI，输出截图并比对既有帧哈希；它需要 MinGW-w64 GCC、CMake、Ninja，只证明 PC 像素等价。场景和参数见 [模拟器说明](../../Tools/gui_simulator/README.md)；推送快照仅复用与固定 SHA256 匹配的 SDL 缓存，否则下载并校验。

## 快照与影响范围

- pre-commit 用 `check_fast.ps1 -Snapshot Index` 验证即将提交的索引，不把暂存后的工作树修正当作已提交证据；手动 FAST 默认验证工作树。
- pre-push 对 ref 更新中的每个待推送 SHA 创建系统临时 detached worktree，从该快照运行检查、规则和测试；原工作树未提交内容不污染它。临时工作树仅在系统临时目录创建和清理。
- shape 分支检查以配置的 writer（否则 origin/HEAD，否则 main）为准；pre-commit 跳过合并/分离 HEAD，pre-push 从 writer 最新 merge-base 判断本分支自己的修改，正常合入 writer 不误报。判定实现见 [`shape_writer.ps1`](../../scripts/shape_writer.ps1)。
- `verification.psd1` 同时维护模块选择、纯文档、全量回退、硬件敏感与 GUI 场景路径；一条路径可命中多个 host 模块。未知非文档路径记录证据并回退全部 host 测试。
- external loader 的 host 几何测试不证明真实下载器可用；生产 loader 还需交叉构建及 CubeProgrammer 真机证据，见其 [README](../../Tools/external_loader/README.md)。
- 修改映射同时更新既有路由自测，先跑 FAST；不在 Hook、Skill 或 reviewer 中另存一份路径表。

## 状态

每个顶层入口只输出一个 `HARNESS_STATUS=`；嵌套入口抑制自身状态行。

| 值 | 含义 |
| --- | --- |
| PASS | 当前 FAST/CHANGED 完成，未发现要求上板的改动路径 |
| PASS_HOST_ONLY | FULL 软件证据完整，命令没有执行上板 |
| NEEDS_HARDWARE_VALIDATION | 软件检查通过，但本次改动仍需板级证据 |
| FAIL | 检查、构建或测试失败 |

仅 FAIL 阻止 commit/push；允许推送待硬件验证状态是协作机制，不代表板级验收。待上板事项按 WF-3 在 tracker 保留证据要求。

## Hook 与工具规范源

克隆后运行 `./scripts/install-git-hooks.ps1` 安装本仓库 Hooks。Git Hook 是提交/推送的最终检查；Agent Hook 提前处理工具调用，边界由 HAR-3/HAR-4 与其脚本维护。

| Agent 事件 | 实现与职责 |
| --- | --- |
| PreToolUse 编辑/Shell | [`guard.ps1`](../../scripts/hooks/guard.ps1)：生成/Vendor、USER CODE、配置头、writer 分支与受保护 Git 操作 |
| PostToolUse 编辑 | [`check-edited.ps1`](../../scripts/hooks/check-edited.ps1)：刚编辑文件的轻量 include 检查 |
| SessionStart | [`session-brief.ps1`](../../scripts/hooks/session-brief.ps1)：分支、脏文件、可实施/待上板事项及 writer inbox 提示 |

生成目录前缀、层级映射分别复用现有检查器；Hook 专有配置头与 USER CODE 范围在 helpers 中维护。Shell 写文件不经过编辑事件，Git 快照检查与独立审阅仍必需。Codex 的项目 Hook 需按内容哈希信任，修改 Hook 后需重新确认。

规范源在 `.agents`，工具包装经 `./scripts/sync-agent-config.ps1` 生成，`-Check` 验证一致性。任务路由、验证和硬件审阅技能只引用领域与验证正文，不复制模块名录或路径映射。

## 独立审阅

非 trivial 功能、重构、Harness 改动由 `independent-verifier` 核对需求、范围与证据；纯拼写和显然无行为影响的小改动可跳过，用户点名时总是执行。硬件敏感行为另由 `embedded-reviewer` 检查。两者均需要时独立并行审阅，不替代确定性脚本或上板；修复后重跑受影响检查，仅当审阅依据改变时再次审阅。

只在某模块出现高风险、重复、独特的操作流程时才考虑新增技能；接口、不变量、术语和技术事实继续由正式文档维护。
