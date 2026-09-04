# CURRENT.md

> 章程：工程现场。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 当前活动：暂停。Queue 窗口单槽已落地，GUI 播放列表未接线。
- 固件最近落地：曲库、顺序播放列表、`storage_listbuffer` 单槽（`request`/`load`）。技术事实见 [catalog_architecture.md](docs/catalog_architecture.md)。
- 活动 scratch：无。
- 阻塞：无。等用户 SquareLine 导出 Queue 列表骨架后再涂行。
- 本主线必读：`docs/gui_ui_design.md`、`docs/catalog_architecture.md`、`docs/coding_standard.md` 第 2 节、`APP/tasks/storage/README.md`、`APP/tasks/storage/catalog/storage_listbuffer.h`。
- 按需查词：曲库 / 播放列表 → **曲库**、**播放列表**。
- `verify.ps1`：已通过（分层 include、生成目录写保护、固件 Debug/Release、主机回归）。

## 本场交接

- 分支：`main`。本场任务为标准化 Agent Harness；不手改 `GUI/`，不改变暂停中的 Queue 功能主线。
- 已落盘到工作副本：FAST → CHANGED → FULL → HARDWARE 入口、索引/推送快照、集中影响规则、pre-push、三个项目 Skill、八份目录级 `AGENTS.md`、两个只读审校者及验证文档。
- 已通过：Harness 路由、分层/生成保护、Windows PowerShell 5.1 真实 pre-commit 索引正反演练、pre-push 提交快照隔离演练、CHANGED 选择性主机测试，以及 FULL 的 Debug/Release 固件构建和 19 项 host 回归。
- 独立审校：`independent-verifier` 已关闭全部通用问题；`embedded-reviewer` 的硬件路径边界均已修复并由路由测试覆盖。
- 后续使用：日常由 Git hooks 自动执行 FAST/CHANGED；发布、合并或要求完整证据时运行 `./scripts/verify.ps1`。硬件敏感改动必须继续上板验证。
