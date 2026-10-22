# Workflow

事项、规划、交接与板上状态放在哪里，以及会话按需读取什么。

Next id: WF-14

## Pillars

- 进度在 tracker，不在仓库文件。
- 按需披露：冷启动只读章程，其余按指针取用。
- 板级事实要有证据才算数。

## Open questions

- SessionStart 简报在离线或 `gh` 未登录时的降级是否够用。

## planning

事项、规格与实施的入口。

### Rules

- **WF-1** · provisional · 事项、spec 与 ticket 都是 GitHub Issues（`immorcoding/mp3_project`），用 `gh` 操作。 _Why:_ 规格、依赖和进度集中，避免多处任务状态失步。 _Source:_ [issue-tracker.md](../agents/issue-tracker.md)
- **WF-2** · exploring · 雾里的大块工作走 `/wayfinder`；已清晰的功能走 `/to-spec` → `/to-tickets` → `/implement`。 _Why:_ 流程深度随不确定性选择，清晰任务无需重复探索。
- **WF-13** · exploring · 一个次版本只做一个用户可见功能；补丁版本（如 0.6.1）只放修复与小打磨，不加新功能；补丁同样以一份 spec 作父事项，只写范围、交付与板级验收，不做设计展开。 _Why:_ 每个版本的范围能在一次集成分支上完成板级验收，避免把多个功能挤进同一版本；集成 PR 与合并板测清单需要父事项（GIT-3）。 _Source:_ [打磨阶段地图](https://github.com/immorcoding/mp3_project/issues/100)

### Signals

- 2026-10-02 · cite · WF-2 · tracker 迁移首次使用 spec #3 与子事项。
- 2026-10-06 · cite · WF-2 · 文档重构通过决策地图与 spec #15 明确职责、入口、清理边界和验收。

## hardware-evidence

板级状态与验收证据。

### Rules

- **WF-3** · provisional · 需要上板的 ticket 挂 `hw:pending`，板上证据作为 issue 评论贴出后再关闭。_Why:_ host 测试证明不了中断、DMA、Cache 与真实外设。
- **WF-4** · provisional · 固件版本、NOR 资源包、待上板项只记在置顶 issue「板上状态」，每次烧录后更新。_Why:_ 设备状态跨 ticket，不属于任何一张。
- **WF-12** · exploring · spec 的每个验收项写明证据来源，且该来源在固件里存在（日志行、计数器、可读寄存器），或明确标“仅听感/外部仪器”；缺固件出口的项在同一 spec 里列为任务。与 WF-3（证据贴为 issue 评论）相邻。_Why:_ #71 板级验收清单要求 `Underruns` 日志作证据，但播放路径除错误外不打日志、`Underruns` 无输出口，板测只能凭听感；固件没有版本输出，测试记录只能写“48f97f0 或 859f307”。_Source:_ #71、#81

### Proposed

- 2026-10-19 · 扩展 WF-3（并入正文或单列待定，level 未定）：板级验收评论使用 `docs/agents/board-test-checklist.md` 模板；结果只用 通过 / 部分 / 未测 / 记录，部分 与 未测 不计通过并保留 `hw:pending`。_Why:_ #81/#87 的两份清单没有统一词汇，证据来源不明导致大量 未测。（queued: 无人值守 drain，chore/board-test-card）

### Signals

- 2026-10-19 · cite · WF-3 · 已有板级验收清单模板 `docs/agents/board-test-checklist.md` 与打卡工具 `Tools/board_test_card/make_card.py`，结果词固定为 通过 / 部分 / 未测 / 记录。
- 2026-10-19 · friction · WF-3 · Underruns 与回调计数无日志（固件版本已由启动日志 `BOOT: FW <提交号>[-dirty] <构建类型>` 输出），B/C/D 组多行只能听感或外部仪器；是否加固件侧诊断出口留给后续决定。

## sessions

会话交接与本地临时材料。

### Rules

- **WF-5** · exploring · 跨会话工作状态在 GitHub spec/ticket 维护，交接用 `/handoff` 写系统临时目录；SessionStart Hook 注入分支、脏文件数与 `ready-for-agent` / `hw:pending` 事项。 _Why:_ 交接不是第二套规范，下一会话应读取当前 tracker 状态。
- **WF-6** · settled · `.scratch/` 只放本地原型，不进 Git。 _Why:_ 原型可快速废弃，不增加产品维护和构建负担。 _Check:_ `.gitignore` 忽略 `.scratch/`。

## standards-evolution

阻塞实施的标准决策与活动文档时效。

### Rules

- **WF-7** · provisional · 规则挡住正在做的 ticket 时按 shape-your-project `INBOX.md`「When a rule blocks the work」升级：决定 issue 挂 `ready-for-human`，阻塞关系用 GitHub `blocked_by`（[issue-tracker](../agents/issue-tracker.md#wayfinding-operations) Blocking）。_Why:_ inbox 要等合并才处理，挡路的规则等不了；本仓库的人工决定状态与阻塞机制需要落到具体标签与 API。_Source:_ shape-your-project `INBOX.md`

- **WF-9** · provisional · 活动文档描述当前有效设计与开发方式；过期操作和排障流水由 Git 历史承载，历史 ADR 明确替代关系后保留；资源按真实依赖判断去留，不另建历史操作归档文档堆。_Why:_ 历史操作与当前设计混排会误导新任务。_Source:_ [旧内容清理决定](https://github.com/immorcoding/mp3_project/issues/13)


## knowledge-ownership

规则、接口、决策与任务材料的长期归属。

### Rules

- **WF-10** · provisional · docs 根 Markdown 收拢至 shape 的适用 area；模块 README 承载局部接口，ADR 保留取舍，tracker 管目标与进度。已有来源承接后删除重复正文，必要时新增 area。 _Why:_ 每种材料有唯一维护位置，入口只负责导航。 _Source:_ [spec #15](https://github.com/immorcoding/mp3_project/issues/15)
- **WF-11** · provisional · 不可替代的硬件事实与格式表在 sources 由规则引用；实现讲解回模块来源，旧章节和操作流水不作为规则 title。 _Why:_ 规则服务未来决定，来源只补充决定所需的事实。 _Source:_ [spec #15](https://github.com/immorcoding/mp3_project/issues/15)

## vocabulary

领域概念的覆盖与短定义。

### Rules

- **WF-8** · provisional · 根 GLOSSARY 从实际跨模块概念反查覆盖，保持短定义与歧义区分。 _Why:_ 旧词条有去向不等于当前领域概念完整。 _Source:_ [spec #15](https://github.com/immorcoding/mp3_project/issues/15)
