# Workflow

事项、规划、交接与板上状态放在哪里，以及会话按需读取什么。

Next id: WF-7

## Pillars

- 进度在 tracker，不在仓库文件。
- 按需披露：冷启动只读章程，其余按指针取用。
- 板级事实要有证据才算数。

## Rules

- **WF-1** · provisional · 事项、spec 与 ticket 都是 GitHub Issues（`immorcoding/mp3_project`），用 `gh` 操作。_Source:_ [issue-tracker.md](../agents/issue-tracker.md)
- **WF-2** · exploring · 雾里的大块工作走 `/wayfinder`；已清晰的功能走 `/to-spec` → `/to-tickets` → `/implement`。
- **WF-3** · provisional · 需要上板的 ticket 挂 `hw:pending`，板上证据作为 issue 评论贴出后再关闭。_Why:_ host 测试证明不了中断、DMA、Cache 与真实外设。
- **WF-4** · provisional · 固件版本、NOR 资源包、待上板项只记在置顶 issue「板上状态」，每次烧录后更新。_Why:_ 设备状态跨 ticket，不属于任何一张。
- **WF-5** · exploring · 不维护 `CURRENT.md` 与 `.scratch/<slug>/{freeze,board}.md`；跨会话交接用 `/handoff`（写系统临时目录）；会话开始由 SessionStart Hook 注入分支、脏文件数与 `ready-for-agent` / `hw:pending` 事项。
- **WF-6** · settled · `.scratch/` 只放本地原型，不进 Git。_Check:_ `.gitignore` 忽略 `.scratch/`。

## Open questions

- SessionStart 简报在离线或 `gh` 未登录时的降级是否够用。

## Signals

- 2026-10-02 · cite · WF-2 · tracker 迁移是第一次走 `/to-spec`：spec #3，sub-issue #4–#7。
