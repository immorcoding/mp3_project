# AGENTS.md

> 章程：始终注入的工作合同。只放闸门、权限与开场协议。功能清单见根 `README.md`；领域词见 `CONTEXT.md`；进度与本场交接见 `CURRENT.md`。超约 100 行则删回指针。

本仓库是基于 STM32H743 的便携式媒体播放器固件。技术文档和代码注释使用中文。不信任对话压缩摘要；硬切后只靠磁盘重建现场。

## 开场

1. 读完整 `CURRENT.md`。
2. 只跟随其中给出的路径（`.scratch/`、ADR、架构文档、术语名）。
3. 术语到 `CONTEXT.md` **按条**查阅；禁止开场整本阅读。
4. 跨层、改边界或改公开 Interface 前，读 `docs/architecture_standard.md` 与相关模块文档。分别判断：功能/抽象所有权、编译期 `#include` 所有权、运行时请求/回调路径。禁止从功能分层图推导 `#include` 方向。

## 权限

写文件、删文件、改 Git 状态（`add`/`commit`/`checkout`/`reset`/`stash`/`push`）、运行会改动仓库的脚本，统称**改动**。改动默认禁止，只有下面定义的许可能解禁；读、搜、编译、跑 `verify.ps1` 不算改动。

**什么算许可。** 许可只来自用户**本条消息**里的可执行指令：有且仅有 **执行**，且点名了模块、文件或功能；范围以点名者为界。以下**都不是**许可：提问、描述现象、抱怨、「看看/分析/为什么/怎么办/评估」、其他的修改类的动词、对话历史里的旧任务、压缩摘要、`CURRENT.md` 里的计划或「下场第一刀」、助手自己发现的问题、编译或验证失败。遇到这些只交付判断和方案，本轮不改动。分不清属于哪一档，按更严的一档执行。

| 档 | 规则 |
| --- | --- |
| 禁止（用户口头同意也不解禁，须由用户本人操作） | 手改生成目录（见下节）；`git commit --no-verify`；设置或写入 `ALLOW_GENERATED_UPDATE`；`git push`；开场整本读 `CONTEXT.md`；范围外顺手重构、格式化、改注释、改命名 |
| 先问 | 改 `AGENTS.md` / `CONTEXT.md`；改 `CURRENT.md` 的**进度**节；新建或修改 ADR；点名范围之外的任何文件；破坏性公开 Interface；`git commit`；CubeMX 文件 `USER CODE` 区；`lv_conf.h`、`FreeRTOSConfig.h`、`ffconf.h` 等生成器配置头 |
| 当前任务可写 | 点名的模块/文件；因实现事实变化必须同步的对应 `*_architecture.md`、该 Module `README.md` 与 `Tests/` 中对应 host 测试 |
| 必须写 | `CURRENT.md` 的**本场交接**（任务替换时、硬切前）；已成立决定同步进正式文档；声称完成前跑通 `./scripts/verify.ps1` |

**先问怎么执行。** 「先问」= 用问题**结束本轮回复**，本轮零改动，等用户下一条消息明确回答后才动。问题必须列出将改动的文件与一句理由。禁止：同一轮自问自答、「先做了再说」、把问题写进回复却继续改、只问其中一部分却改全部、用「用户应该会同意」替代询问。

**改动前自检。** 每轮首次改动前，先在回复里写出：(1) 许可来自用户哪条消息、哪个动词、点名了什么；(2) 本轮将改动的文件清单及各自所属档；(3) 任一文件落在「先问」或「禁止」→ 整轮不改动，转为提问。自检写不出来就没有许可。

**许可生命周期。** 新的可执行改动**替换**旧许可；「继续」「可以」「好」或回答确认**不替换也不扩大**；「顺便改 X」在当前许可上**追加** X。任务完成（verify 通过并汇报）后许可失效，后续改动重新申请。硬切由用户发起；建议硬切前必须先写本场交接。

## 严禁：生成目录只认生成器

两套生成器与 Vendor 代码都是**唯一事实来源**，助手一律不手改，只给配置步骤，由用户修改、导出、验证：

| 来源 | 事实源 | 产物 | 助手能做的 |
| --- | --- | --- | --- |
| SquareLine | `SquareLineProject/` | `GUI/` | 给组件、布局、样式、事件步骤；每推进一步先更新 `docs/gui_ui_design.md` |
| CubeMX | `io_sheet.ioc` | 文件头带 ST 生成声明或含 `USER CODE BEGIN/END` 的文件：`Core/`、`FATFS/`、`USB_DEVICE/` 中的生成文件，`cmake/stm32cubemx/`、`startup_*.s`、`*.ld`、`.mxproject`、`FreeRTOSConfig.h`、`stm32h7xx_hal_conf.h` | 给外设、引脚、时钟、NVIC、DMA/MDMA 参数的配置步骤；参数落到对应架构文档 |
| Vendor（HAL/CMSIS/中间件） | 上游发布 | `Drivers/STM32H7xx_HAL_Driver/`、`Drivers/CMSIS/`、`Middlewares/ST/`、`Middlewares/Third_Party/{LVGL,FreeRTOS/Source,FatFs}` | **无任何例外**，没有 `USER CODE` 区；HAL 行为不合适一律在 `Adapters/stm32_hal/` 包一层或改 `hal_conf`/`.ioc` 配置；升级由用户整体替换 |

唯一例外：CubeMX 文件中 `USER CODE BEGIN`/`END` 之间。这里只准放对自维护入口的调用或转发，不放产品逻辑（见 `docs/architecture_standard.md` 3.1）；属「先问」档，且任务必须点名该文件。标记之外一个字符也不改，「修复导出结果」「临时原型」「只是加个 include」「改一行参数」「HAL 有 bug 打个补丁」都不是理由——参数改 `.ioc` 后重新生成，HAL 缺陷在 Adapter 层绕过并记入架构文档。`FATFS/Target/bsp_driver_user_diskio.*` 是自维护文件，不在此列。

写保护由 `scripts/check-generated-write.ps1` 在 FAST 执行，但它**不覆盖** `Core/`、`FATFS/`、`USB_DEVICE/`、`.ioc`、`.ld`、`startup_*.s`；这些路径同样受本节约束，不得以「Hook 没拦」为准。仅维护者本人可在本机命令行对生成目录提交使用 `--no-verify`，或在当前 PowerShell 会话设置 `$env:ALLOW_GENERATED_UPDATE = '1'` 后提交 SquareLine / CubeMX 重新导出。不要把该变量写入用户或系统环境变量；Git Graph 带不上它。

## 完成前验收

未运行且通过 `./scripts/verify.ps1`（固定等价 FULL），不得声称软件验证完成；硬件敏感变化还必须明确保留上板状态。FAST → CHANGED → FULL → HARDWARE 的入口、状态和路径映射见 [verification.md](docs/verification.md)。只有 `FAIL` 阻止 Hook；`PASS_HOST_ONLY` 与 `NEEDS_HARDWARE_VALIDATION` 均不得冒充板级验收。

克隆后执行一次 `./scripts/install-git-hooks.ps1`。`pre-commit` 对 Git 索引运行 FAST，`pre-push` 对实际推送提交运行 CHANGED。非 trivial 变化完成后用只读 `independent-verifier`；DMA、Cache、ISR、RTOS、HAL、Platform、链接段或硬件生命周期变化再用只读 `embedded-reviewer`。两者不进入 Hook。`git commit` 标题必须为 `YYYY/M/D HH:MM` + 一句中文，不写正文；细则见 [coding_standard.md](docs/coding_standard.md) 第 6 节。

## 三份根文档限制

| 文件 | 篇幅 | 只准 | 不准 |
| --- | --- | --- | --- |
| `AGENTS.md` | 约 100 行 | 开场协议、权限、严禁、验收、路径 | 功能清单、领域定义、进度、分层图、ADR/架构正文 |
| `CONTEXT.md` | 全文可长；单条约 25 行 | 稳定术语、职责边界、相关术语、一句示例 | 进度、脏文件、`#include` 方向、寄存器/坐标/缓存大小 |
| `CURRENT.md` | 合计约 80 行，单节约 40 行 | 主线、路径、阻塞、下场第一刀、分支/脏文件名、verify、未落盘未决项 | 抄 ADR/架构/`CONTEXT` 正文、贴 diff、功能清单 |

超限：从本文件删回指针；`CONTEXT.md` 单条下沉到 `*_architecture.md` 或 Module README；`CURRENT.md` 外溢到 `.scratch/` / ADR / 架构文档。

## 指针

- 功能清单（给人看，非必要不读）：根 `README.md`
- 术语词典：`CONTEXT.md`（按条查）
- 现场：`CURRENT.md`
- 文档地图：`docs/README.md`
- 分层验证与 Agent Harness：`docs/verification.md`
- 冷启动与 ADR 范围：`docs/agents/domain.md`
- 本地事项：`docs/agents/issue-tracker.md`；状态：`docs/agents/triage-labels.md`
- 命名、注释与提交标题：`docs/coding_standard.md`

## 目录级约定

进入下列目录工作时，继续读取路径上最近的子 `AGENTS.md`；子文件只补充本地 Seam 和验证要求，不覆盖本章程：

- `Components/`、`Adapters/`、`Platform/`、`Service/`、`APP/`、`Tests/`、`scripts/`；
- 独立板级产物 `Tools/external_loader/`。

不在 CubeMX、Vendor、`GUI/` 或 `SquareLineProject/` 生成目录放置子 `AGENTS.md`；这些目录继续遵守本文件的生成边界和写保护。
