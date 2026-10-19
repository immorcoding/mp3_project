# Inbox: chore/check-doxygen

> 以下是 2026-10-19 把 STY-2 的机械部分落成 `scripts/check-doxygen.ps1`（接入 FAST / CHANGED）时记下的 Signal 与待办；规则正文未改。drain 时提给用户。

## Signal

- 2026-10-19 · code-style · cite · STY-2 · `scripts/check-doxygen.ps1` 检查“函数定义紧邻 `/** */`、有 `@brief`、每个形参有 `@param`、非 void 有 `@return`/`@retval`”，新函数另要求 `@param` 带方向；只报本次差异（暂存区 / 工作树 / `BaseRef..HeadRef`）里新增或修改过的函数，存量不报。对 `git log` 最近 20 个提交回放：触及 141 个函数，2 处报告（均为 `Tools/gui_simulator/sim_main.c` 的 `main` 无文档，属真阳性），0 处误报；最近 150 个提交回放共 907 个函数 132 处报告，抽查 `HAL_I2S_*Callback`、`storage_listbuffer_load`、`sim_pump_events` 等均为当时确实缺项。
- 2026-10-19 · code-style · friction · STY-2 · 全仓库存量（`./scripts/check-doxygen.ps1 -Report`，只计数不失败）：300 个手写 C/H 文件、1517 个函数定义，其中 112 个缺文档或缺项（Tools 29、Service 24、Tests 24、Adapters 16、Components 9、APP 8、Platform 2；不含方向）。这些函数下次被改动时会被检查拦住，需顺手补齐，不做集中清欠。
- 2026-10-19 · harness · signal · HAR-5 · 检查器的盲区（写在脚本头注释）：K&R 旧式定义、返回函数指针的定义、无返回类型的宏包装（`TEST_CASE(x) {`）无法解析时静默跳过；文档与函数之间有空行或其他注释算未紧邻；纯删除（删光紧邻函数上方的文档且无新增行）不会被发现。描述措辞、阻塞性、所有权、线程/ISR 等语义仍只能靠 `code-review`。

## 待办（writer branch drain 时执行）

- 修改 `docs/shape/code-style.md` STY-2 的 _Check:_：把“`CODING_STANDARDS.md` 的注释审阅入口”改为“`scripts/check-doxygen.ps1`（FAST）；`CODING_STANDARDS.md` 只保留判断性部分（描述措辞）”。规则正文不变。STY-5 的 _Check:_ 仍是审阅入口，不动。
- `docs/shape/sources/verification.md` 的入口表补一行：`./scripts/check-doxygen.ps1` · 自维护 C/H 中新增或修改的函数定义须有紧邻中文 Doxygen（STY-2 机械项）；支持 `-Snapshot Index`、`-BaseRef/-HeadRef`，`-Report` 只打印全仓库存量计数、不失败。同页 `check_fast.ps1` 一行“含文档机械检查”可顺带加上 Doxygen。
- `docs/shape/harness.md` HAR-5 的 _Check:_ 若要列检查脚本，可在 FAST 一栏加 `check-doxygen.ps1`；否则以 verification.md 为准即可。
- 路径路由无需改 `scripts/rules/verification.psd1`：`scripts/` 前缀已落入 `FullHostTestPathPatterns`，`harness_helpers.Tests.ps1` 新增了对应的路由断言。
