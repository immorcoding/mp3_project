# ui_cell 主机测试

本目录验证 `APP/ui_cell/ui_cell.h` 的公开行为（ARC-24，#217）和持锁统计（DIAG-6，#218）。测试链接真实 `ui_cell.c`、`ui_cell_stats.c` 和共用 FreeRTOS 桩（`Tests/stubs/freertos/`，桩目录放在 `../..` 之前），统计部分还链接脚本化的周期计数器与 Service Log 替身（`Tests/stubs/lock_env/`），只经公开函数观察，不看内部字段。模块把唯一的互斥锁藏在内部，测试用桩的 `freertos_stub_last_created_semaphore()` 取得句柄，再用“让接下来 N 次拿取失败”和阻塞钩子脚本化别的任务占着锁；模块不为此提供任何测试入口。

覆盖（对应票的验收条目）：

- 整份拷贝：写入后读者拿到全部字节；写入时拷走数据，之后改动源缓冲不影响格子；新读者（已见序号 0）拿到一次初值。
- 序号：每次写入都推进，内容相同的重写也算变化；连写两次读者只读到最新的一份；已见序号由读者持有，两个读者互不影响。
- 没变不拷贝：读者副本不被改写、已见序号不变，并且不碰锁（锁的拿取次数不增加）。
- 拿不到锁：不等的写返回假，格子数据、序号都不变、不叫醒，且拿锁超时为 0（不等）；读返回假，读者副本和已见序号不变，下一次能读到；等到拿到锁的写在别的任务占着锁时一直等（超时为无限），放开后写入成功。
- 一把锁：多个格子共用同一把，第二个格子初始化不创建新锁；每个操作返回时锁都已放开。
- 叫醒：每次成功写入调用一次叫醒函数，且调用时锁已放开；读不叫醒；失败的不等写不叫醒；传空不叫醒、写入照常成功。
- 写者唯一（调试版断言）：同一任务反复使用两种写、不同格子各有各的写者都不触发；第二个任务写同一个格子触发断言。断言走共用桩的 `configASSERT`（与产品同形，失败进桩里的 `vAssertCalled` 后 `abort()`），测试用 `SIGABRT` 处理函数加 `longjmp` 捕获，运行时 stderr 会出现两行 `configASSERT failed: .../ui_cell.c:NN`，这是预期输出。

7 个 CTest：`ui_cell_behavior`（上面的行为用例，同一进程，#218 起还核对 `ui_cell_changed()` 与 `ui_cell_boot_init()`：序号变了才为真、不拿锁；锁在 `ui_cell_boot_init()` 里创建，之后第一个 `ui_cell_init` 不再创建新锁）；`ui_cell_stats_*`（5 个，每个一个进程，Debug 配置）：`hold_max`（持锁最长只取单次持有的最大值，不含叫醒函数）、`wrap`（周期计数器 32 位回绕不让持锁虚高）、`gui_tries`（GUI 一侧的不等入口——`try_write`、叫醒函数为空的格子的 `read_if_changed`——的尝试与拿不到锁分开计数，前台读需求格不计）、`poll_period`（汇总行至多每 10 s 一行、没有新数据不投、格式 `n=… hold=…us gui=… busy=… pct=…`）、`zero_gui`（没有 GUI 尝试时占比为 0.0）；`ui_cell_release_has_no_stats`（`-DNDEBUG`，发布构建里统计是空操作：不投行、`ui_cell_stats_get` 恒为 0）。

保真边界：主机测试是单线程的，PASS 证明“拿到、拿不到、等到”各分支上行为正确，不证明真实并发：不证明读者在写者持锁中途读不到半份数据（单线程下不会交错）、不证明优先级继承、不证明持锁时长（8 KiB 量级的 `memcpy` 在 Cortex-M7 上的耗时）、不证明 GUI 拿不到锁的频率。这些进板级验收（清单 3，R5.3）。带 `NDEBUG`（固件 Release）时写者检查被编译掉，行为用例以 `-UNDEBUG` 构建，不覆盖 Release 的字段布局；发布构建只由 `ui_cell_release_has_no_stats` 核对统计关闭。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module ui_cell
```
