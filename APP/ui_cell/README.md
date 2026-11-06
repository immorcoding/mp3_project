# ui_cell

GUI 与任务之间交接界面数据的短锁模块。一个“格子”装一份固定大小的数据；写者整份拷入，读者整份拷出，读者读不到写了一半的内容，GUI 永远不等锁。决定见 [ARC-24](../../docs/shape/architecture.apps.md)（ARC-25 为 exploring）与 [#202](https://github.com/immorcoding/mp3_project/issues/202) E 节。0.10.0 前台一次切（[#218](https://github.com/immorcoding/mp3_project/issues/218)）起有了使用者：音乐的曲名需求、曲名结果、窗口需求、窗口结果四个格子（`APP/apps/music/music_cells.h`），GUI 写需求、前台任务写结果。

## Interface

`ui_cell.h`，APP 私有，`Service/gui` 不得包含（`scripts/check-layer-includes.ps1` 检查）。

| 函数 | 谁用 | 行为 |
|---|---|---|
| `ui_cell_boot_init()` | Boot Task（`create_task` 直接调） | 先创建全模块唯一的互斥锁（静态存储）。创建静态互斥量要进 FreeRTOS 队列的发送路径，调用链深，Boot 栈只有 128 word，所以放在最浅的位置；不调用也行，`ui_cell_init` 第一次调用时会补（核算见 [前台任务](../tasks/foreground/README.md)“Boot 栈核算”） |
| `ui_cell_init(cell, storage, size, wake)` | Boot Task（注册表的 `boot_init`） | 绑定调用者的存储与大小，传叫醒函数（结果格传 `NULL`）；锁没创建就创建 |
| `ui_cell_write(cell, src)` | 前台、后台任务 | 等到拿到锁，整份拷入、序号加一，锁外叫醒 |
| `ui_cell_try_write(cell, src)` | GUI | 不等；拿不到锁返回 `false`，格子和序号都不变，下一帧再试 |
| `ui_cell_read_if_changed(cell, dst, seen)` | 任何读者 | 不等；序号等于读者的 `*seen` 就不碰锁、返回 `false`；变了才拿锁拷贝并更新 `*seen`；拿不到锁返回 `false` 且 `dst`、`*seen` 都不变 |
| `ui_cell_changed(cell, seen)` | 读者 | 不拿锁地问“有没有新的一份”（序号与 `seen` 不同）。读者在 `ui_cell_read_if_changed()` 返回 `false` 后用它区分“没变”和“锁被占着没读到”：后者要尽快重试，否则没有人再叫醒读者，需求会一直停在格子里（前台任务读需求格就是这样处理，step 返回 1 ms） |
| `ui_cell_stats_get(out)`、`ui_cell_stats_poll(now_ms)` | 证据出口 | 持锁统计的累计值与汇总日志，见下节；发布构建里恒为 0 和什么都不做 |

不提供锁函数，也没有 enter/leave。持锁区间里只有 `memcpy` 和序号推进，不调用文件接口、叫醒函数或任何会等待的东西，所以“持有界面数据锁时调用文件接口”写不出来。它和文件锁是两把锁，两组调用先后执行，不嵌套。

叫醒函数在锁外调用，可能运行在 GUI 或后台任务里，不得阻塞。初始化传函数而不是任务句柄，因为 Boot Task 准备格子时任务还没创建。

## 持锁统计（DIAG-6，R5.3；Debug 开、Release 关）

开关是 `UI_CELL_LOCK_STATS_ENABLE`（`ui_cell_config.h`），口径同 Filesystem 的持锁统计和曲库短锁统计：固件 Release 带 `-DNDEBUG` 整段编译掉，不量时间、不加计数、不投日志。记三样（开机以来的累计，最大值不清零）：

- **持锁最长**（微秒，DWT 周期计数器）：只含锁内的 `memcpy` 与序号推进，不含叫醒函数和断言，所以量的是别的任务等这把锁的最长时间上界；
- **拿到锁的次数**；
- **GUI 一侧的不等入口的尝试次数和拿不到锁的次数**：GUI 一侧指 `ui_cell_try_write()`（GUI 写需求格）和叫醒函数为空的格子（结果格，读者是 GUI）的 `ui_cell_read_if_changed()`（序号变了才算尝试，没变化的帧不碰锁、不计）。前台任务读需求格不计入；占比 = `busy / gui`。

汇总行由前台任务每圈调 `ui_cell_stats_poll()` 投（GUI 帧路径上不投日志，DIAG-3），至多每 10 s 一行，没有新数据不投：`I (<tick>) UICELL: n=<拿锁次数> hold=<持锁最长 µs>us gui=<GUI 尝试次数> busy=<没拿到的次数> pct=<busy 占比，一位小数>`，例如 `UICELL: n=1203 hold=14us gui=8450 busy=3 pct=0.0`。最长 70 个字符。板级验收 R5.3 读最后一行：`hold` ≤ 5 ms，`pct` ≤ 1 %（真实负载下，持锁的是 6 KiB 量级的拷贝，推算数 µs 到几十 µs，未测）。

## 使用边界

1. **需求是状态，不是动作。** 连按翻页由 GUI 算出目标页再写；真有不能合并的动作，带递增的动作序号或另用队列。新的需求覆盖旧的。
2. **一个格子只有一个写者任务**，可以有多个读者（音乐里需求格只有 GUI 写、结果格只有前台任务写）。调试版用 `configASSERT` 检查第一次写入之后其他任务写同一格子。“调试版”的开关是 `NDEBUG`：固件 Release 带 `-DNDEBUG`（CMake 的 Release 默认参数 `-O3 -DNDEBUG`），Debug 不带；带 `NDEBUG` 时写者字段和检查整体编译掉。
3. **约 8 KiB 以内进格子**（初值，ARC-25，exploring）。持锁时长随大小线性增长，所以更大的数据由应用持有两块缓冲，格子只传该看哪一块；封面是第一个要走通的例子，还没有实现。本模块不限制大小。
4. **结果过期就会显示错内容时才比序号。** 曲名和封面的结果要带回所答的需求序号，GUI 只采信最新的；列表窗口自带起点、代次和条数，又是整份拷贝，不比。这是应用的事，本模块只提供“变了才读”。
5. **存储由调用者提供**，大小由调用者的缓冲决定；本模块不在栈上放整份数据拷贝。初值在 `ui_cell_init` 之后、第一次读之前放好（静态存储为全零）；序号从 1 起，所以全新的读者（`seen` 为 0）会拿到一次初值。
6. 模块不认识应用和内容：结构体由各应用定义（音乐的放在 `APP/apps/music/music_cells.h`）。需求格的叫醒函数是 `foreground_task_wake`；结果格不唤醒 GUI，传空，GUI 最慢 16 ms 看一次。

## 并发与已知限制

- 序号是 32 位对齐变量，读者不拿锁比较；在 Cortex-M7 上单次读是原子的，这也是不拿锁比较序号的前提。比较之后到拿锁之间序号可能又变，读者在锁内重读序号，所以 `*seen` 总是与拷到的那份一致。
- 锁是互斥锁（带优先级继承）。没有选关调度（会让播放任务一起等），也没有选纯序号校验（谁都不等，但依赖内存屏障、难验证）。锁藏在模块内，上板发现问题时可以换实现，应用代码不动。
- 序号回绕要 2^32 次写入，不处理。
- 主机测试见 [Tests/ui_cell](../../Tests/ui_cell/README.md)；真实并发、持锁时长和 GUI 拿不到锁的频率进板级验收（R5.3）。

## 编译期依赖

- FreeRTOS：`FreeRTOS.h`、`semphr.h`（静态互斥锁）、`task.h`（仅调试版，取当前任务句柄做写者断言）；
- `Adapters/cortex/cycle_counter` 的 DWT 周期计数器与 `Service_Log_Post()`（只有持锁统计用，发布构建不引用）；
- 标准库：`<string.h>`、`<stdio.h>`（只有统计汇总行用）。断言用产品的 `configASSERT`（`FreeRTOSConfig.h`，无条件生效，失败进 `vAssertCalled`），模块不使用 `<assert.h>`。
