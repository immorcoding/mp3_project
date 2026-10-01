# AGENTS.md

本仓库是基于 STM32H743 的便携式媒体播放器固件。技术文档、代码注释与对用户的回复使用中文。本文件是 Claude Code 与 Codex 共用的唯一章程（Claude Code 经 `CLAUDE.md` 引入）；只放硬规则与指针，正文按需读取。

## Project shape

当前标准，每个领域一个文件；动到哪个领域先读哪个文件。标准的增改走 `shape-your-project` skill。

- [Harness](docs/shape/harness.md)：双轨章程、Skill、Hook 与分层验证
- [Workflow](docs/shape/workflow.md)：GitHub 事项、spec/ticket、板上状态
- [Architecture](docs/shape/architecture.md)：分层、include 方向、生成器边界
- [Git](docs/shape/git.md)：远端、分支、Conventional Commits
- [Code style](docs/shape/code-style.md)：命名、Doxygen、config 排版

## 硬规则

- **生成目录只认生成器**（ARC-2）。不手改下表产物，只给配置步骤，由用户修改源工程、导出、验证：

  | 来源 | 事实源 | 产物 |
  | --- | --- | --- |
  | CubeMX | `io_sheet.ioc` | 带 ST 生成声明或 `USER CODE BEGIN/END` 的文件：`Core/`、`FATFS/`、`USB_DEVICE/` 中的生成文件，`cmake/stm32cubemx/`、`startup_*.s`、`*.ld`、`.mxproject`、`FreeRTOSConfig.h`、`stm32h7xx_hal_conf.h` |
  | Vendor | 上游发布 | `Drivers/STM32H7xx_HAL_Driver/`、`Drivers/CMSIS/`、`Middlewares/ST/`、`Middlewares/Third_Party/{LVGL,FreeRTOS/Source,FatFs}`，无任何例外 |

  唯一例外是 CubeMX 文件 `USER CODE` 区，只放对自维护入口的调用或转发（ARC-3），改前先问用户。`FATFS/Target/bsp_driver_user_diskio.*` 是自维护文件。
- **不绕过闸门**：不用 `git commit --no-verify`，不强推任何分支（需要时由用户本人执行），不设置或写入 `ALLOW_GENERATED_UPDATE`（只有维护者本人在当前 PowerShell 会话为 CubeMX 重新导出设置）。Agent Hook 会拦截这些操作（HAR-4）。
- **范围内改动**：不顺手重构、格式化、改注释或改命名；改公开 Interface、`CONTEXT.md`、ADR 或生成器配置头（`lv_conf.h`、`FreeRTOSConfig.h`、`ffconf.h`）前先问用户。
- **三张图分开判断**：跨层、改边界或改公开 Interface 前读 `docs/architecture_standard.md` 与相关 Module README，分别判断功能/抽象所有权、编译期 `#include`、运行时请求/回调路径。
- **界面手写**（ADR-0016）：全部 Screen 在 `Service/gui/view/` 手写，不用图形化工具生成代码；界面每推进一步先更新 `docs/gui_ui_design.md`。

## 完成前验收

声称软件验证完成前，`./scripts/verify.ps1`（固定等价 FULL）必须通过；`PASS_HOST_ONLY` 与 `NEEDS_HARDWARE_VALIDATION` 都不是板级验收，需要上板的事项挂 `hw:pending`（WF-3）。层级、状态与路径映射见 [verification.md](docs/verification.md)。克隆后执行一次 `./scripts/install-git-hooks.ps1`。

改动 `Service/gui/` 后还要跑 `./Tools/gui_simulator/run-scenarios.ps1`（模拟器场景回归；CHANGED/pre-push 命中 GUI 路径时也会自动跑）；帧哈希变化须看过截图、确认是有意的，才用 `-Update` 重写基线，并在提交说明写明原因。它只证明 PC 上像素等价，不是板级验收。

非 trivial 变化完成后调用只读审阅者 `independent-verifier`；DMA、Cache、ISR、RTOS、HAL、Platform、链接段或硬件生命周期变化再调用 `embedded-reviewer`。提交格式见 GIT-2。

## 指针

- 事项、进度与板上状态：GitHub Issues，约定见 `docs/agents/issue-tracker.md`，标签见 `docs/agents/triage-labels.md`
- 术语词典：`CONTEXT.md`（按条查，不整本读）；读取顺序与 ADR 范围：`docs/agents/domain.md`
- 文档地图：`docs/README.md`；功能清单（给人看）：根 `README.md`

## 目录级约定

进入下列目录工作时，继续读取路径上最近的子 `AGENTS.md`（Claude Code 经同目录 `CLAUDE.md` 引入）；子文件只补充本地 Seam 与验证要求，不覆盖本章程：

- `Components/`、`Adapters/`、`Platform/`、`Service/`、`APP/`、`Tests/`、`scripts/`；
- 独立板级产物 `Tools/external_loader/`。

生成目录不放子 `AGENTS.md`。
