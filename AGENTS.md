# AGENTS.md

> 章程：始终注入的工作合同。只放闸门、权限与开场协议。功能清单见根 `README.md`；领域词见 `CONTEXT.md`；进度与本场交接见 `CURRENT.md`。超约 100 行则删回指针。

本仓库是基于 STM32H743 的便携式媒体播放器固件。技术文档和代码注释使用中文。不信任对话压缩摘要；硬切后只靠磁盘重建现场。

## 开场

1. 读完整 `CURRENT.md`。
2. 只跟随其中给出的路径（`.scratch/`、ADR、架构文档、术语名）。
3. 术语到 `CONTEXT.md` **按条**查阅；禁止开场整本阅读。
4. 跨层、改边界或改公开 Interface 前，读 `docs/architecture_standard.md` 与相关模块文档。分别判断：功能/抽象所有权、编译期 `#include` 所有权、运行时请求/回调路径。禁止从功能分层图推导 `#include` 方向。

## 权限

默认先问再写盘。许可只来自当前**未完成**的那条可执行任务。

| 档 | 规则 |
| --- | --- |
| 禁止 | 手改 `GUI/`、`SquareLineProject/`；`git commit --no-verify`；把 `ALLOW_GENERATED_UPDATE` 写入用户或系统环境变量；开场整本读 `CONTEXT.md`；范围外顺手重构 |
| 先问 | 改 `AGENTS.md` / `CONTEXT.md`；改 `CURRENT.md` 的**进度**主线；新建或修改 ADR；当前任务范围外的文件；破坏性公开 Interface |
| 当前任务可写 | 用户这条任务点名的模块/文件；因实现事实变化必须同步的对应 `*_architecture.md` 与该 Module `README.md` |
| 必须写 | `CURRENT.md` 的**本场交接**（任务替换时、硬切前）；已成立决定同步进正式文档；声称完成前的 `./scripts/verify.ps1` |

许可生命周期：新的可执行改动**替换**旧许可；「继续」「可以」或回答确认**不替换**；「顺便改 X」在当前许可上**追加**。硬切由用户发起；建议硬切前必须先写本场交接。

## 严禁与 GUI

`GUI/` 与 `SquareLineProject/` 以 SquareLine 工程为唯一事实来源。助手只给编辑器中的组件、布局、样式和事件步骤，由用户编辑、验证并导出。GUI 设计每推进一步，必须先更新 `docs/gui_ui_design.md`。生成目录写保护见完成前验收。

仅维护者本人可在本机命令行对生成目录提交使用 `--no-verify`，或在当前 PowerShell 会话设置 `$env:ALLOW_GENERATED_UPDATE = '1'` 后提交 SquareLine / CubeMX 重新导出。不要把该变量写入用户或系统环境变量；Git Graph 带不上它。

## 完成前验收

未运行且通过 `./scripts/verify.ps1`，不得声称完成。该命令先做分层 `#include` 检查和生成目录写保护，再构建固件 Debug/Release 并跑主机回归。只改 `Components/`、`Adapters/`、`Platform/` 或 `Service/` 的包含关系时，可先跑 `./scripts/check-layer-includes.ps1`。只核对应保护生成目录是否被手改时，可先跑 `./scripts/check-generated-write.ps1`。

克隆后在仓库根执行一次 `./scripts/install-git-hooks.ps1`。`pre-commit` 跑分层检查和生成目录写保护，失败则拒绝提交。`git commit` 标题必须为 `YYYY/M/D HH:MM` + 一句中文，不要另写正文；细则见 [coding_standard.md](docs/coding_standard.md) 第 6 节。

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
- 冷启动与 ADR 范围：`docs/agents/domain.md`
- 本地事项：`docs/agents/issue-tracker.md`；状态：`docs/agents/triage-labels.md`
- 命名、注释与提交标题：`docs/coding_standard.md`
