# Harness

Claude Code 与 Codex 双轨共用的章程、Skill、审阅 Agent、Hook 与分层验证。

Next id: HAR-16

## Pillars

- 一份事实源，两个薄入口：正文只写一处，工具目录只放指针或生成物。
- 能机制拦截的不写成文字：闸门优先落在 Git Hook、Agent Hook 与检查脚本。
- 软件证据不冒充板级证据。

## Open questions

- Codex 项目级 Hook 需在 Codex 内信任一次（按内容哈希），Hook 改动后需重新确认；是否需要在克隆说明里提示。

## entrypoints

共同章程、规范源与按需读取。

### Rules

- **HAR-1** · provisional · `AGENTS.md` 是唯一章程；根与子目录的 `CLAUDE.md` 只写 `@AGENTS.md`。_Why:_ Codex 原生读 `AGENTS.md`，Claude Code 读 `CLAUDE.md`。
- **HAR-2** · settled · 仓库若有自定义 Skill/Agent，正文只放 `.agents/skills/` 或 `.agents/reviewers/`，工具包装由 `scripts/sync-agent-config.ps1` 生成；当前评审复用根 AGENTS「完成前」指定的已安装审阅 skill，仓库不维护专用 reviewer。`settings.json`、`hooks.json` 仍手工维护。_Why:_ 保留单一正文与生成一致性，避免另建重复评审入口。_Check:_ FAST 运行 `sync-agent-config.ps1 -Check`，包括空配置与受管残留检查。
- **HAR-9** · provisional · 根 AGENTS 保留项目与语言约定、shape 使用方式、关键保护边界摘要、条件阅读指针和验收入口；子 AGENTS 只补目录独有操作要求。目录路由描述职责与必读正文，模块 README 维护接口和使用约束；正文一处维护，入口引用已落地的目标。_Why:_ 避免根、子入口与规范重复而漂移。_Source:_ [开发入口决定](https://github.com/immorcoding/mp3_project/issues/12)

- **HAR-12** · provisional · 新增 Skill 仅用于反复出现且高风险的独特操作流程，接口、术语和不变量继续由领域文档维护。 _Why:_ 说明型 Skill 会增加常驻导航负担并复制事实源。 _Source:_ [开发入口决定](https://github.com/immorcoding/mp3_project/issues/12)
- **HAR-14** · exploring · Claude Code 按根 AGENTS「模型分工」表（唯一来源）按阶段选模型与 effort；ticket 带 `visual` 标签时整张改由视觉实施行负责，判定口径只在 `docs/agents/triage-labels.md`。_Why:_ 规划与实施的推理需求不同；Sonnet 在外观判断上不及 Opus。_Source:_ 用户决定 2026-10-08、2026-10-09

## hooks

工具守卫与即时检查。

### Rules

- **HAR-3** · provisional · Agent Hook 两边共用 `scripts/hooks/*.ps1`，同时解析 Claude 的 `file_path` 与 Codex `apply_patch` 的补丁文本，路径以 Hook 输入的 `cwd` 为基准规范化；生成/Vendor 前缀复用 `check-generated-write.ps1`，分层映射复用 `check-layer-includes.ps1`，Hook 专有的 CubeMX 源、配置头与 USER CODE 根只在 `scripts/hooks/agent_hook_helpers.ps1`。 _Why:_ 两种工具的输入不同，但保护边界必须一致且只维护一份。 _Source:_ Codex `core/src/tools/hook_names.rs`（`apply_patch` 以 `Edit`/`Write` 为 matcher 别名）
- **HAR-4** · provisional · PreToolUse 硬拦生成/Vendor 目录写入与 `--no-verify`、强推、`ALLOW_GENERATED_UPDATE`；CubeMX `USER CODE` 文件、生成器配置头与自维护链接脚本（`*.ld`）在 Claude 走 `ask`、在 Codex 一律 `deny`；PostToolUse 只做毫秒级单文件检查；不在 Stop 跑构建。_Why:_ Codex 对 `ask` 会记错误后放行；固件构建慢，每轮触发不可承受。_Source:_ Codex `hooks/src/events/pre_tool_use.rs`（`unsupported_permission_decision_fails_open`）、commit `e57187b`（强推判定只看 `git push` 之后的参数）

## verification

验证层级与主机自测隔离。

### Rules

- **HAR-5** · settled · 验证分 FAST（pre-commit）→ CHANGED（pre-push）→ FULL（`verify.ps1`）→ HARDWARE；声称完成前 FULL 通过；`PASS_HOST_ONLY` 不是板级验收（各状态字含义见 verification.md）。 _Why:_ 快速反馈与完整软件证据分层执行，硬件结论另需真实设备。 _Source:_ [verification.md](sources/verification.md) _Check:_ FAST、CHANGED 由 `install-git-hooks.ps1` 安装的 `pre-commit`、`pre-push` 自动运行，路径路由在 `scripts/rules/verification.psd1`；FULL 在声称完成前手动运行。
- **HAR-6** · settled · 自测夹具挂起继承的 `GIT_*` 环境变量；主机编译器只读 `MP3_HOST_CC`。_Why:_ worktree 中 Hook 导出的绝对 `GIT_DIR` 曾让夹具写坏真实仓库；通用 `CC` 常被其他项目设为 Clang。_Source:_ commit `e1be57c` _Check:_ `scripts/build_helpers.ps1` 的 `Suspend-InheritedGitEnvironment` 挂起 `GIT_DIR` 等 7 个仓库定位变量；`test-host.ps1` 只读 `MP3_HOST_CC`。

- **HAR-10** · settled · 非 trivial 功能、修复、重构（含文档与规则迁移）及 Harness 改动统一用根 AGENTS「完成前」指定的审阅 skill，固定 diff 基线，由未参与编写的规范轴与需求轴审阅者分别核对适用规则、需求、范围、验证覆盖及原始证据；硬件敏感变更在规范轴纳入嵌入式清单。作者自查单列；仅拼写、链接等小改可豁免，用户点名时总是审阅；修复后重跑受影响检查，审阅依据改变后复审。_Why:_ 自动化检查与独立判断各司其职，统一流程保留专项风险覆盖。_Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 提供领域指针，审阅报告分别记录发现、验证证据与板级待验事项。
- **HAR-11** · provisional · 验证路径只在 verification.psd1 维护，变更同时更新路由自测。_Why:_ 重复路径表会制造第二份事实源，路由变更需要独立证据。

- **HAR-13** · provisional · 最终验证证据记录提交 SHA、工作树状态、命令、退出码和完整输出；交付时把证据归档到 PR 附件或制品位置，不以系统临时目录作为唯一留存。 _Why:_ 摘要不能独立证明实际验收快照，临时文件也会被清理。 _Source:_ [spec #15](https://github.com/immorcoding/mp3_project/issues/15)
- **HAR-15** · exploring · Vendor 补丁以仓库根 `patches/<上游>/` 下的 patch 文件维护；CubeMX 重新导出或上游更新后，先跑 `scripts/apply-vendor-patches.ps1`（可重复执行：已应用跳过、能干净应用则应用、冲突则停下报位置）再跑 FAST；FAST 以 `git apply --reverse --check` 确认每个补丁完整生效，否则报“<上游> 补丁未应用”（如“FatFs 补丁未应用”）并给出重打命令；补丁落到 Vendor 目录由用户设 `ALLOW_GENERATED_UPDATE` 后提交（HAR-4），把含该提交的分支推送或合入集成分支也由用户在设该变量的会话里执行。 _Why:_ 重新导出会静默覆盖补丁，等到链接失败才发现时看不出原因；提交前检查能直接给出原因与重打命令。_Source:_ [ADR-0022](../adr/0022-fatfs-open-by-locator-patch.md)、[打开文件提速](https://github.com/immorcoding/mp3_project/issues/167)

### Proposed

- 2026-11-16 · provisional · HAR-11 补一句：主机工程共用的 FreeRTOS 桩在 `Tests/stubs/freertos/`（头文件按产品 include 路径摆放，带脚本化接口：拿不到锁、阻塞钩子、tick 与通知观察；自测是 `Tests/flash_ftl` 的 `freertos_stub_behavior`），新增依赖 FreeRTOS 的主机工程把 `../stubs/freertos` 放在 include 路径最前，并在 `verification.psd1` 登记 `^Tests/stubs/freertos/`。_Why:_ 并发模块各带一份桩会各自漂移；路径不登记，改桩就选不中受影响的模块。（queued: 无人值守 drain，#203 inbox）
- 2026-11-16 · provisional · HAR-5 补一句：要证明的是“对外部入口的调用顺序”（例如卷任务先卸载、再发卷消失、最后才开闸）时，把循环体拆成单步函数（如 `volume_sd_step(now, edge)`：吃一圈的输入、返回下一圈最多等多久），用链接期假实现记录顺序和调用那一刻的状态，`for(;;)` 外壳只留创建和等待。_Why:_ `for(;;)` 没法测，纯函数又证明不了调用顺序。（queued: 无人值守 drain，#211 inbox）

### Signals

- 2026-11-16 · cite · HAR-5 · 0.10.0 的并发模块（Filesystem 锁与完成等待、卷、后台、前台、音频读取、`ui_cell`、曲库）的主机测试用共用 FreeRTOS 桩脚本化阻塞与顺序：PASS 只证明分支、调用顺序与状态更新时机，不证明任务抢占、优先级继承、真实时序、DMA/Cache 和栈余量，`*_task.c` 的 `for(;;)` 外壳没有主机测试；这些由第 7 次板测（#221）的证据承担，模拟器的假后端同样测不到“新需求覆盖旧需求”。
- 2026-11-06 · friction · HAR-5 · pre-push 把待推送提交检出到 `%TEMP%\mp3-push-<guid>\`（根路径约 75 字符）再跑主机测试，那里没有 FULL 用的短根映射（`short_root.ps1` 只接在 `verify.ps1` 上）；主机工程的对象路径加上目标名和跨目录源文件会超过 MAX_PATH（260），报 `opening dependency file … .obj.d: No such file or directory`，看上去像测试失败（#218 第一次推送就这样失败）。处理：缩短 `Tests/music_foreground` 的构建目标名。约定候选：构建目标名要短，`2 × 根长 + 目标名 + 源相对路径` 不超过 259，或让 pre-push 的临时 worktree 也走短根映射。
- 2026-11-06 · friction · HAR-6 · 手工 `cmake -S Tests/<模块> -B <目录>` 不带 `-DCMAKE_C_COMPILER` 时本机选到 Clang 加调试运行库，`assert` 失败的 `abort()` 弹模态对话框，进程挂着等人点；Harness 经 `MP3_HOST_CC` 用 GCC，没有这个问题。手工构建要带 `-DCMAKE_C_COMPILER=gcc`；`Tests/audio_reader` 的新测试用本目录的 `host_test_assert.h`（失败时打印位置、`exit(1)`）。
- 2026-10-17 · cite · HAR-5 · 播放链路主机测试的保真边界：`Tests/playback_task` 的假播放模块不模拟半区、欠载与 Stop/Start 顺序，PASS 只证明状态机对播放模块 Interface 的使用与反应；外壳 `playback_task.c` 无主机测试，只由固件构建编译与静态断言覆盖；`Tests/playback_service` 的假 Platform 音频有唯一不忠实开关 `fake_audio_accept_rate_for_test()`（放宽 22.05 kHz 以覆盖流尾补 0）。
- 2026-10-19 · cite · HAR-5 · `scripts/check-doxygen.ps1`（STY-2 机械项）的盲区：K&R 旧式定义、返回函数指针的定义、无返回类型的宏包装（`TEST_CASE(x) {`）静默跳过；文档与函数间有空行或其他注释算未紧邻；纯删除文档不会被发现。描述措辞、阻塞性、所有权、线程/ISR 等语义仍靠 `code-review`。
- 2026-10-19 · cite · HAR-11 · 主机模块 `PathPatterns` 手写会随 include 漂移；`scripts/check-test-paths.ps1`（FAST）对每个模块只配置 `Tests/<模块>/`、读 `compile_commands.json` 并以 `gcc -MM` 取依赖，要求每个仓库内依赖被该模块某个 PathPatterns 命中，首次运行补齐 22 处缺口并加路由断言；约 4.7 s。
- 2026-10-19 · friction · HAR-5 · `check-test-paths.ps1` 发现的缺口集中在跨层聚合头与配置头（`Service/service.h`、`Platform/platform.h`、`APP/app_config.h`、`Service/log/log_service*.h` 等），一改就选中 4~6 个模块，CHANGED 变慢但正确；盲区：仅在别的宏定义下才包含的头、`file(READ)` 改写输入、链接脚本、测试数据与 `CMakeLists.txt` 不经 `#include`，过宽的模式不检查。
- 2026-10-19 · cite · HAR-5 · 新主机模块 `monitor_warn`（`APP/tasks/monitor/monitor_warn.c`）的保真边界：PASS 只证明未登记任务告警的去重/容量/截断逻辑；`monitor_task.c` 外壳（任务名到栈深的登记表、`Service_Log_Post` 的 WARN）无主机测试，靠固件构建与板上 MONITOR 日志；漏登记是运行期 WARN，不是编译期保证。
- 2026-10-31 · friction · HAR-15 · 补丁文件必须以 LF 存放，否则 `git apply` 在 `core.autocrlf=true` 的 Windows 工作树上无法与 CRLF 文件比对；已在 `.gitattributes` 加 `patches/**/*.patch text eol=lf`，可补进 HAR-15。
- 2026-10-31 · friction · HAR-15 · 在仓库子目录（如 `build/host/<模块>/`）里对副本 `git apply` 会把补丁路径当作仓库根相对路径而 Skipped；主机测试用 `GIT_CEILING_DIRECTORIES` 绕开，且副本已含补丁（用户提交 Vendor 改动之后）时先 `--reverse --check` 跳过。
- 2026-11-01 · cite · HAR-5 · 仓库根超过 56 个字符时 FULL（`verify.ps1`）自动 `subst` 到空闲盘符运行、结束含失败卸载，证据目录与 metadata 仍写真实路径（#183，`scripts/short_root.ps1`）。
- 2026-11-01 · cite · HAR-11 · `verification.psd1` 的主机模块可设 `Kind = 'Python'`（unittest 模块，如 `board_test_card`），测试目录 `TestDirectory`、慢用例 `HasSlowTests` 也在该文件登记；CHANGED 传 `-SkipSlow` 跳过慢用例，FULL 全跑（#183）。

## collaboration

改动授权与领域文件写入位置。

### Rules

- **HAR-7** · provisional · 改动许可只由工具自身模式（Claude 默认/acceptEdits，Codex workspace-write + on-request）与 Hook 决定。_Why:_ 文字许可协议耗上下文且拦不住。
- **HAR-8** · provisional · 领域文件 `docs/shape/*.md` 只在写入分支（默认 `main`）修改，规则编号也只在那里分配；其他分支把 Signal、草稿、用户当场的批准和改名造成的过期链接（`link` 行；`check-docs` 对已登记的断链放行）写进 `docs/shape/inbox/<分支>.md`，合并后在 `main` 上按 drain 处理。inbox 每行按 shape-your-project `INBOX.md` 写 `日期 · area[/title] · kind · 内容`，只放会改变规则或 ROUTES 的行；`hw:pending` 事项与票结果只记在 issue 与置顶「板上状态」#8（WF-3、WF-4）。_Why:_ 并行分支同时改领域文件会冲突，相同的 `Next id` 改动会被 git 静默合并成重号。_Source:_ shape-your-project `INBOX.md`

### Proposed

### Signals

- 2026-10-20 · friction · HAR-8 · #98 把链接脚本改名为 `STM32H743xG_flash.ld`，`hardware.md` 的两处链接必须与改名同一提交，否则 `check-docs` 断链、FULL 失败；pre-commit 的写入分支检查拦住了分支上的链接修改。用户批准的绕法：先在 `main` 把两处链接改成纯文本文件名（`7f53d80`），再合入分支。
- 2026-11-04 · friction · HAR-8 · 0.10.0 要搬走 `APP/tasks/storage/`（曲库到 `APP/apps/music/catalog/`，卷与基准到 `APP/tasks/volume/`，音频流到 `APP/tasks/audio_reader/`），领域文件不能在分支上改，所以第 0 步先把 `storage.catalog.md`、`storage.volumes.md`、`sources/storage-media.md`、ADR-0018 里指向它的链接改成纯文本，合入 `main` 后再补（#222 补回）。这是上面 Proposed 那条的第二次证据。
- 2026-11-16 · decision · HAR-8 · 用户决定改名引起的规则链接走 inbox `link` 行（shape-your-project 的过期链接，[skills#33](https://github.com/immorcoding/skills/issues/33)、[skills#34](https://github.com/immorcoding/skills/pull/34)），`check-docs` 对 inbox 已登记的断链放行，写入分支检查不变；取代 2026-10-20 那条“分支随改名直接改链接”的待决提议。
