# Inbox: ticket-217-ui-cell

- 2026-11-05 · routes · route · ROUTES.md 的 `Routed:` 行与条目新增 `APP/ui_cell/`：界面数据短锁模块（GUI 与任务之间的交接，APP 私有）。Entry: [ui_cell](../../../APP/ui_cell/README.md)。Areas: [Architecture](../architecture.md)（ARC-24、ARC-25，正文 [architecture.apps](../architecture.apps.md)）。`Tests/` 条目新增主机工程 `Tests/ui_cell`（[README](../../../Tests/ui_cell/README.md)）。依据：#217 新增 `APP/ui_cell/`，现在没有使用者。
- 2026-11-05 · architecture · proposed · ARC-24 已含“锁不出模块、持锁只拷贝内存、一格一个写者”，现已实现：全模块一把静态互斥锁（第一次 `ui_cell_init` 时创建），写者唯一用调试版断言（记下第一次写入的任务，Release 无字段无代码），`Service` 不得包含 `ui_cell` 由 `scripts/check-layer-includes.ps1` 的 `service` 种类硬检查（含不带 `APP/` 前缀的写法）。建议 ARC-24 补一句“Service 不得包含由分层检查强制”（#217）。
- 2026-11-05 · architecture · proposed · ARC-24 / #193 的草案签名在实施时调整了两处，建议把正文的签名同步：叫醒参数是函数 `void (*)(void)`（类型 `UiCell_WakeCallback_t`）而不是任务句柄加标志；`ui_cell_read_if_changed` 的格子参数是 `const`。序号从 1 起，所以全新读者（`seen` 为 0）会拿到一次初值（#217）。
- 2026-11-05 · harness · proposed · HAR-11 补充：被测模块把锁藏在内部时，共用 FreeRTOS 桩新增 `freertos_stub_last_created_semaphore()`（#217），测试在被测模块初始化后取句柄，再用 `freertos_stub_semaphore_fail_next_takes` 与阻塞钩子脚本化“拿不到锁”。注意模块的静态锁只在第一次初始化时创建，`freertos_stub_reset` 之后不再创建，测试要把句柄缓存下来（见 `Tests/ui_cell/test_ui_cell.c` 的 `setup`）。
- 2026-11-05 · harness · proposed · `GuiScenarioPathPatterns` 暂不加 `^APP/ui_cell/`：模拟器现在不编译 ui_cell，没有使用者；等 #200 的假后端改成写格子、模拟器开始编译它时再加（规则文字见 #202 “路径规则”一行）。
