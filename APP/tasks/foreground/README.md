# Foreground Task

前台任务轮流调用注册表里各应用的 `foreground_step`，取最小返回值当睡眠时长，任何通知都提前叫醒，订阅卷事件，并量每个 step 的耗时。所有应用共用这一个任务：一步太长会拖住别的应用，没有锁或断言能拦住，所以靠耗时记录发现（ARC-26）。决定见 [ADR-0023](../../../docs/adr/0023-task-split-and-app-contract.md)、[architecture.apps](../../../docs/shape/architecture.apps.md)（ARC-23 至 ARC-27）与 [#202](https://github.com/immorcoding/mp3_project/issues/202) E、G 节；0.10.0 前台一次切（[#218](https://github.com/immorcoding/mp3_project/issues/218)）时落地，随后只有音乐一个应用，前台流程见 [music](../../apps/music/README.md)。

分工规则：会等的不进 GUI，吃 CPU 的不进前台任务。前台任务只做“读一行文件、拷内存、发布界面数据”这类短活，不碰 Flash 卷，不做 CPU 重活（重活进后台任务）。

## 公开 Interface

- `foreground_task_create()`（`foreground_task.h`）：静态创建，由 `app_tasks.c` 的 Boot 任务调用，**必须晚于 `app_table_boot_init()`**（它的优先级 2 高于 Boot，创建后立即运行，第一圈就会调各应用的 step，格子和缓存键表必须已准备好）。
- `foreground_task_wake()`：APP 内部接口（[#202](https://github.com/immorcoding/mp3_project/issues/202) M 节授权），不是 Service 接口。叫醒前台任务，让它马上把所有应用的 step 各调一遍。任何任务上下文可调，不得从 ISR 调；任务还没绑定句柄时是空操作（第一圈本来就调所有应用）。叫醒的来源有三：GUI 写需求（`ui_cell` 的叫醒回调，音乐的两个需求格传它）、曲库发布（`catalog.c` 发布成功后叫醒一次）、卷事件（任务入口登记订阅，卷就绪和卷消失都叫醒）。
- 注册表的 `foreground_step`（`APP/apps/app_table.h`）：`uint32_t step(void)`，返回“多少 ms 后再叫我”：0 马上，`FOREGROUND_WAIT_FOREVER`（`UINT32_MAX`）等通知。每圈每个应用恰好调一次（返回 0 的 step 不会让后面的应用被跳过），空字段跳过。
- `foreground_loop.h`（任务内部，可主机测试）：`foreground_loop_run_steps()`、`foreground_loop_step()`、`foreground_loop_step_max_us()`、`foreground_loop_report()`，以及 `app_table_boot_init_rows()`（`APP/apps/app_table_boot.c`，Boot 逐行调用 `boot_init`）。

## 一圈做什么

```text
for(;;)
├─ foreground_loop_run_steps()      每个应用的 foreground_step 各一次，按表里的顺序；量每次的耗时
├─ ui_cell_stats_poll() 与 foreground_loop_report()   持锁统计与 step 最长值的汇总行（到点且有新数据才投）
└─ 等通知，超时 = 各 step 返回值的最小值（0 不等，WAIT_FOREVER 一直等）
```

第一圈不等通知（先调一遍 step）：任务晚于第一条需求或第一个卷事件启动也不会漏，各应用的 step 自己比对卷序号、重看最新需求。叫醒用 `eSetBits`：连叫几次合并成一次醒；step 运行期间到达的通知留在槽里，等待立刻返回，不会丢。通知槽 `FOREGROUND_NOTIFY_EVENT` 里有两个位：`FOREGROUND_NOTIFY_FLAG_WAKE`（`foreground_task_wake()`）和 `FOREGROUND_NOTIFY_FLAG_VOLUME`（卷事件），FLAG 只用于唤醒，醒来后以曲库代次和需求格为准。

## 前台的一小步（ARC-26）

- **一步最多一次文件调用加约 1 ms CPU**（初值），step 里不放大局部变量（大对象放 `.apps_sdram`，栈上只有几十字节的局部变量），不碰 Flash 卷（Flash 卷只有一把锁，后台写 32 KiB 约 1.5 s，前台一等所有应用的 step 都停）。**音乐现在的实现一步读一整行标签（5 至 11 次 Service 调用），与“一次文件调用”字面不符，见下文“已知偏差”，留给用户决定。**
- 任务量每个 step 的耗时（DWT 周期计数器，32 位无符号差，回绕不影响），记下**每个应用的最长值**；**超过 50 ms（`FOREGROUND_STEP_WARN_US`，初值，R5.6 核对）且刷新了该应用的最长值时记一行 WARN**，只记录不停机（所以一个持续偏慢的应用不会刷屏，SD 单块 500 ms 超时这类故障路径也不会误伤：它只刷新一次最长值）。
- 没超阈值的最长值刷新**不立刻记**，汇总成一行 INFO，至多每 10 s 一行（`FOREGROUND_MAX_LOG_PERIOD_MS`），没有新刷新就不记，供板级验收读各应用的最长耗时（R5.6）。两种行里的应用名取自注册表的 `name` 字段（这就是 `name` 字段的用途），消息池核算里计 1 行。
- 计数器没启动（频率 0）时耗时记 0，不会误报。

## 日志（串口里每行是 `<时间> <I|W|E> (<tick>) <标签>: <正文>`，标签后面有冒号；常开）

| 行 | 级别 | 时机 | 节拍 |
|---|---|---|---|
| `FOREGROUND: Foreground step slow app=<应用名> us=<微秒> limit=50000` | WARN | 某应用 step 耗时刷新最长值且超过 50 ms | 每次刷新最长值一行，单调，不刷屏 |
| `FOREGROUND: Foreground step max <应用名>=<微秒>us ...` | INFO | 最长值有刷新但没超阈值，且距上一行满 10 s | 至多每 10 s 一行；行放不下时末字符是 `~`；只列有样本的应用 |
| `FOREGROUND: Volume event subscription failed.` | ERROR | 订阅表满（不会发生） | 开机 1 次 |

应用名取自注册表的 `name` 字段（音乐是 `music`）。整行不超过 127 字符，主机测试在长应用名下核对。

## 优先级与交错

前台任务优先级 2（用户 2026-11-06 的票面：前台 2；GUI、Log、Monitor 1 → 2；Storage Task 2 → 1）：

| 优先级 | 任务 |
|---|---|
| 4 | 播放 |
| 3 | 音频读取、卷、定时器服务（一直阻塞） |
| 2 | 前台、GUI、Log、Monitor、Flash 启动（一次性） |
| 1 | Storage（曲库扫描、Flash 回收）、后台 |
| 0 | Boot（自删）、IDLE |

和各任务的交错（都是纸面推算，**主机测试是单线程的，证明不了真实交错，靠第 5 次上板**）：

- **播放（4）**：随时抢占前台；播放不碰文件、不拿任何文件锁，所以前台在卷锁上的等待不会被它拖成反转。前台的 step 只拿到播放解码之外的 CPU。
- **音频读取（3）和卷（3）**：都比前台高，读取任务一读一块（32 KiB，持 SD 锁约 7 ms，实测的 SD 读 32 KiB 持锁最长 6.85 ms）就抢占前台。前台要读标签时在 Filesystem 入口等 SD 卷锁：锁被读取任务持有时前台睡着等（互斥锁带优先级继承，持锁的低优先级任务临时升到前台的级别）。**水位以上**读取任务每读一块用唯一的等待点让 1 tick（让的是 SD 锁），前台这时拿得到锁，一次标签读取（至多几次读，初值 4 次）的等待推算 14–27 ms；**水位以下**（切歌后补块期约 140 ms）读取任务连续补块不让路，前台一行标签读取要等到补块间隙，这是 R5.4 要核对的最坏情形（上限推算 50 ms 内）。卷任务消抖对账期间 Filesystem 入口在闸前等约 30 ms，前台的文件调用也在闸前等，step 因此变长，不记为故障（WARN 只记录）。
- **GUI（2）**：同级，时间片轮转（`configUSE_TIME_SLICING` 开，1 ms）。GUI 每圈睡 1 至 16 ms，没活就让出，所以两者同时就绪的时间很短，交替各占一个 tick。前台一步 ≤ 约 1 ms CPU 加一次文件调用（文件调用在等锁或等 DMA 时是睡着的，不占 CPU），不会把 GUI 一帧拖过刷新周期。
- **`ui_cell` 的短锁**：GUI 的写和读都是不等的（`try`），拿不到下一帧再试，所以 GUI 永远不等；前台的写（`ui_cell_write`）等到拿到为止，持锁者只拷内存（窗口格约 6 KiB，推算数 µs 到几十 µs，未测，R5.3 实测），可能的最坏是持锁的 GUI 被音频读取或播放抢占，前台等到它回来放锁（同级的互斥锁继承不升高优先级，等待上界是被抢占的时间，不是死锁）。前台读需求格也是不等的：读不到而序号变了时 step 返回 1 ms，自己重试，因为没有人会再叫醒它（`ui_cell_changed()`）。
- **Storage 与后台（1）**：前台和 GUI 空闲时才跑。Storage 扫描曲库时持 SD 锁调目录读取，前台要读标签就等那一次调用（持锁的 Storage 经优先级继承临时升到 2，直到放锁）；大目录路径打开最长约 2 s，但曲库里的文件一律走定位符，不走这条路。后台的 Flash 作业只碰 Flash 卷，不挡前台。
- **Log、Monitor（2）**：同级，各自很短（Monitor 每 5 s 一批约 20 行，几 ms）。Log、Monitor 从 1 升到 2 是票面要求，让日志不被 GUI 和前台饿着。

结论：前台与 GUI 同级且各自都是“短步、不等”，没有互相饿死的路径；前台对 SD 锁的等待由读取任务的让路（1 tick）和水位决定，是第 5 次上板要量的数字（R5.4：前台等 SD 锁 ≤ 50 ms）。**这些优先级和步长都是纸面决定，由板级验收修正。**

## 栈、内存与段

| 项 | 位置 | 大小 |
|---|---|---|
| 前台任务栈 | AXI SRAM 新段 `.app_axi_ram`（`xTaskCreateStatic`，栈由内核填成已知值，HWD-2） | 1536 word = 6144 B（初值，R5.7） |
| 前台任务 TCB | DTCM `.bss` | 约 100 B |
| 音乐的四个界面数据格子本体（曲名需求、曲名结果、窗口需求、窗口结果） | `.app_axi_ram` | 6588 B（`music_cells.c`） |
| GUI 一侧的结果副本（窗口、曲名） | `.app_axi_ram` | 6572 B（`gui_music.c`） |
| 行缓存的行内容（64 行）与键表（64 个 `{代次, 位置}`）、窗口组装缓冲、曲名结果组装区、曲名读取用的临时一行（504 B）、曲库一次读全的结果（约 300 B）、前台流程的状态变量 | `.apps_sdram`（SDRAM，NOLOAD，`ASSERT` 卡 8 MiB；ARC-28：应用内存默认放这里，DTCM 留给任务栈与内核） | 40224 B（`music_row_cache.c`、`music_foreground.c`） |

**段总量**（Debug 构建 map，`mp3_project.map`）：`.app_axi_ram` 0x9380 = **37760 B**（五个任务栈 24576 B：卷 4096、音频读取 4096、Flash 启动 4096、后台 6144、前台 6144；加格子本体 6588、GUI 副本 6572、没有别的贡献者；起点 `0x2404b600`），比上一票多 19328 B；`.apps_sdram` 0x9d20 = **40224 B**（上限 8 MiB；审阅修复里把前台流程的状态、曲库读全结果、行缓存键表和曲名临时一行从 DTCM 与任务栈挪进来，比第一版的 0x97c0 = 38848 B 多 1376 B）。内存区（Debug，`build/firmware-debug`，五个钩子都显式关）：AXI RAM 区 346496 / 524288 B（66.09 %，余 177792 B）；DTCM 109408 / 131072 B（83.47 %，余 21664 B；比上一票 3eac3e6 的 113912 B 少用 4504 B：窗口槽、名称槽迁出约 6.5 KiB，日志消息池加大 15 块加约 2 KiB，其余是新状态；审阅修复把 864 B 的前台状态与缓存键表挪到 `.apps_sdram`，池多一块加 136 B，净少 720 B）；SDRAM 8597568 B（25.62 %）；内部 Flash 474404 / 524288 B（90.49 %，余 49884 B）。Release：DTCM 108936 B（83.11 %），Flash 381040 B（72.68 %）。曲名读取用的 504 B 临时行原来是 `process_title` 的局部变量，在前台任务栈上（AXI 段，不占 DTCM），现在挪走后栈上没有大局部变量。链接脚本、`FreeRTOSConfig.h`、CubeMX 生成文件、Vendor、`ffconf.h`、`lv_conf.h` 一个字没改。

NOLOAD 段不清零：栈靠 `xTaskCreateStatic` 填充（`app_tasks.c`、`foreground_task.c` 各有一个 `_Static_assert` 核对这个前提），格子和 `.apps_sdram` 里的缓冲与状态由 Boot 里的 `boot_init` 在第一次使用前整块写入，行缓存的行内容靠键表判断有效（键表在 `boot_init` 里清零）。格子、副本和 SDRAM 状态只经 CPU 读写，不作 DMA，不需要 Cache 维护。

### Boot 栈核算（128 word = 512 B）

新增的最深调用链有两条（`-fstack-usage` 用工程的真实编译命令，Debug 与 Release 各量一次；一次任务切换最坏要在任务栈上留约 204 B，见 [卷任务](../volume/README.md)“Boot 栈核算”）：

| 链 | 函数与帧（Debug） | 合计（Debug） | 加任务切换 | Release 合计 | 加任务切换 |
|---|---|---|---|---|---|
| ① `create_task` 直接调 `ui_cell_boot_init()`：创建界面数据锁（静态互斥量，进队列发送路径） | `create_task` 48、`ui_cell_boot_init` 8、`ensure_lock` 8、`xQueueCreateMutexStatic` 40、`prvInitialiseMutex` 16、`xQueueGenericSend` 48、`prvCopyDataToQueue` 32、`xTaskPriorityDisinherit` 32 | 232 B | **436 B** | 约 170 B | **约 375 B** |
| ② `app_table_boot_init()` 逐行调 `boot_init` | `create_task` 48、`app_table_boot_init` 8、`app_table_boot_init_rows` 24、`music_boot_init` 8、`music_cells_boot_init` 8、`ui_cell_init` 24（锁已存在，不再进队列，加一次 `memset` 约 8 B） | 128 B | **332 B** | 约 105 B | **约 310 B** |

两条都小于 512 B（Debug 余量 76 B 和 180 B）。**这就是为什么锁由 `create_task` 直接创建**：如果把创建留给注册表里的第一个 `ui_cell_init`，链 ② 会叠上创建链，静态调用图最深约 520 B，超出 512 B（`-fcallgraph-info` 的保守读法，包含 `xQueueGenericSend` 的阻塞分支 `xTaskResumeAll`，实际不走）。静态调用图（含从不执行的阻塞分支）链 ① 最深约 456 B。Boot 栈没有运行时读数（Boot 自删，Monitor 读不到它），这是静态核算，不是实测；我没有改 `APP_BOOT_TASK_STACK_WORDS`。往 Boot 里加初始化调用时，核对“最深链 + 204 B”不超过 512 B。

## Monitor 登记

`monitor_task.c` 的登记表加了 `Foreground Task`（栈 1536 word），名字只在 `APP/app_config.h` 的 `APP_FOREGROUND_TASK_NAME` 定义一次，创建处（`foreground_task.c`，带 `_Static_assert` 核对长度）和 Monitor 都引用它：**正好 15 个字符**（`configMAX_TASK_NAME_LEN` 为 16，再长一个字符会被截成登记不上的名字，每批出 `Task not registered in monitor` WARN）。CPU 占比行的短键是 `fore`，与 `crea`、`log`、`stor`、`moni`、`gui`、`play`、`idle`、`tmr`、`back`、`audi`、`volu`、`flas` 互不相同。同一批里可能同时出现的任务是 12 个（永久任务 11 加开机后几秒的 Flash 启动任务），CPU 行在极端组合（十个任务各占 10.0 %）下一位小数的完整格式是 129 字符，放不下，所以 `monitor_cpu_format()` 在放不下时整行退化成整数百分比（`gui=10`），保证一行放得下；平常占比保持一位小数，格式不变。核算见 [Monitor](../monitor/README.md)。

## 日志与消息池核算（DIAG-3）

加前台任务之后新增的常开或 Debug 行：Monitor 一批多一行栈占用；前台 step 耗时的 WARN 或最长值汇总（二选一，1 行；注册表的 `name` 字段就是这两种行里的应用名，没有它日志里分不出是哪个应用的 step 慢）；`ui_cell` 持锁统计（Debug，每 10 s 1 行）；显示延迟汇总（Debug，每 10 s 1 行）；前台遇到 `SERVICE_BUSY` 的 WARN（异常路径，第 1 次和之后每满 100 次各 1 行，约 1 s 一行，见下）。最坏同时到达行数（按 [音频读取任务](../audio_reader/README.md)“日志与消息池”的保守口径）从 29 行变成 34 行，Service Log 消息池从 30 块加大到 **45 块**：

| 项 | 行数 |
|---|---|
| Monitor 一批（开机后 `MountFlash` 期间 12 个任务加 5 行固定行） | 17 |
| Filesystem 持锁统计 | 3 |
| 曲库短锁统计 | 1 |
| `ui_cell` 持锁统计（本票） | 1 |
| 显示延迟汇总（本票） | 1 |
| 前台 step 耗时（WARN 或汇总，本票） | 1 |
| 一次拔卡（保守口径：卷路径日志 5、读取被打断、`Stop reason`、`CARD_REMOVED lat`） | 8 |
| `SERVICE_BUSY`（异常路径：音频读取任务与前台任务各 1 行，文件槽用尽时会同时到） | 2 |
| **最坏同时到达** | **34，留 11 块**（池 45） |

Flash 压力钩子构建再加统计行 1、卷满 1、停下 1 共 3 行，37 行，留 8 块；翻页注入钩子构建再加补满耗时 1 行，35 行，留 10 块。池深在 `Service/log/log_service_config.h`，`monitor_task.c` 的 `_Static_assert` 核对 `MONITOR_TASK_CONCURRENT_MAX + 5 + MONITOR_POOL_OTHER_WORST_LINES + MONITOR_POOL_SPARE_MIN <= LOG_SERVICE_QUEUE_LENGTH`（12 + 5 + 17 + 8 = 42 ≤ 45），每加一个任务或一种常开日志行就要重算。排空按每 5 ms 一行，34 行约 170 ms。

**前台的 `SERVICE_BUSY` 日志**（WARN，标签 `MUSIC`，`SERVICE_BUSY pos=<位置> n=<连续第几次>`）：BUSY 是瞬时的，前台一律过约 10 ms 重试，不设次数上限、不显示回退名（ARC-27），所以日志必须节制：连续 BUSY 只在第 1 次和之后每满 `MUSIC_BUSY_LOG_EVERY`（100）次各记一行，读到非 BUSY 的结果后计数清零。BUSY 的终止靠卷事件、曲库代次变化或新需求（窗口以空窗收尾、曲名答代次 0）。正常情况下这一行不应出现（SD 同时最多 2 个文件，见 [音频读取任务](../audio_reader/README.md) R4.12），出现了就是文件槽被别处占着，R5、R6.10 核对日志里没有它。

## 翻页注入测试钩子（DIAG-7，R5.4，默认关闭，只允许 Debug）

验证“水位与让路”：切歌后音频读取任务在补块（补到安全水位约 140 ms，推算），这时前台读一窗标签要等 SD 卷锁。钩子让这一幕在切歌后马上发生，不靠人手：GUI 音乐分区每次看到新一首开始播放（快照的 `TrackStartCount` 变化），自动写一次翻页需求（窗口起点前进一窗，到列表末尾回到 0），记下注入时刻；前台任务每个 tick 回头量块池就绪块数，回到安全水位（4 块）时记一行补满耗时，5 s 还没补满记一行 WARN 放弃。

- 写法照前几个钩子：`if (MUSIC_HOOK_PAGE_TURN != 0U)`（`APP/apps/music/music_config.h`，默认 `0U`），根 `CMakeLists.txt` 缓存变量 `MP3_HOOK_PAGE_TURN`（默认 0，**只允许 Debug**，非 Debug 构建开会在 configure 时报错）。开：`cmake --preset firmware-debug -DMP3_HOOK_PAGE_TURN=1`；关：`-DMP3_HOOK_PAGE_TURN=0`（缓存变量会留在构建目录里，开过要显式关）。
- 启动日志一行 WARN：`W (<tick>) HOOK: page_turn ON watermark=4 timeout=5000ms`（标签后面有冒号，搜 `HOOK: page_turn ON`；没有这一行就是没开）。它在前台任务的第一步里投。
- 数据行：`I (<tick>) HOOK: page_turn refilled after=<ms>ms ready=<块> free=<块>`，超时 `W (<tick>) HOOK: page_turn timeout after=5000ms ready=… free=…`。`after` 从 GUI 看到新曲那一帧算起（GUI 最慢 16 ms 看一次快照，所以注入比切歌命令被处理的瞬间晚 0 至约 16 ms，**不是票面写的 2 ms**），分辨率 1 ms。
- 前台等 SD 锁的时间看持锁统计（`FSLOCK` 行里 `wait=` 的最长值）。让路改 0 做对比：把 `AUDIO_READER_YIELD_TICKS` 改成 0（`audio_reader_config.h`）重新构建，不算钩子。
- 默认构建里钩子的代码被编译但分支恒假，GUI 一侧只多一次比较。

## 主机测试证明什么、不证明什么（HAR-5）

`Tests/foreground_task`（1 个 CTest `foreground_loop`，14 个用例）：证明每圈每个 step 各一次、按表里的顺序、返回 0 也不短路、空字段跳过；睡眠时长取最小返回值、没有 step 时等通知；毫秒换 tick（非零不变成 0）；一圈是“调 step、调每圈回调、再等通知”，回调先于等待；睡眠期间和 step 运行期间的叫醒都不丢；句柄未绑定时叫醒是空操作；每个应用各记最长值、只升不降；超过 50 ms 且刷新最长值才记 WARN（恰好等于阈值、相等、更短都不记，再刷新再记，返回值照常）；WARN 行和汇总行不超过 127 字符，放不下时末尾 `~`；汇总至多每 10 s 一行且没有新刷新不记；计数器没启动和 32 位回绕不误报；注册表 `boot_init` 按表的顺序逐行调用、空字段跳过。**不证明**：真实任务切换与优先级继承、通知唤醒延迟、与 GUI 同级时的 CPU 份额、step 的真实耗时、Boot 栈余量（静态核算）、`foreground_task.c` 的静态创建和入口（没有主机测试，由固件构建和上板覆盖）。音乐前台流程见 [Tests/music_foreground](../../../Tests/music_foreground/README.md)。

## 已知偏差：一步读一整行，不是一步一次文件调用（审阅意见，留给用户决定）

ARC-26 / spec G 写“前台一步最多一次文件调用加约 1 ms CPU”。现在的实现一步读**一整行标签**：`catalog_track_text_read()` 在一步里连续做完“打开、取长度、至多 4 次读取、关闭”，行与行之间才是步边界（检查点）。这是按“一行标签读取”作为最小单位的取舍（#135 I 节的一行标签读取判定），不是一次 Service 调用。

**一行读取实际的 Service 调用次数**（每次调用各拿一次 SD 卷锁、各过一次 Filesystem 入口闸）：

| 调用 | 次数 |
|---|---|
| `Service_Filesystem_OpenFileByLocator`（核对不符时再多一次 `OpenFile` 按路径） | 1（核对不符 2） |
| `Service_Filesystem_GetFileSize` | 1 |
| 读回调：`SeekFile` 加 `ReadFile`，每次读取各一次（含首块最多 `CATALOG_TEXT_MAX_READS` = 4 次读取） | 2 至 8 |
| `Service_Filesystem_CloseFile` | 1 |
| **合计** | **5 至 11**（带 TIT2/TPE1 的常见文件在首块里一次读完，5 次；核对不符再加 1） |

**推算的 step 最长耗时**（没有实测，R5.4、R5.6 取证；Debug 单步含 DWT 计时的开销可以忽略）：

- 无竞争：打开约几 ms（O1 的阈值是 30 ms）、每次 4 KiB 读取约 1 至 3 ms，整行约 10 至 20 ms，慢卡的上界约 45 ms。
- 水位以上与音频读取任务争 SD 锁：读取任务每读一块让 1 tick，前台每次调用最多多等一次让路加一块读（1 至 7 ms），整行推算 14 至 27 ms，上界约 50 ms 量级（超过 50 ms 就出 `Foreground step slow` WARN）。
- 水位以下（切歌后约 140 ms 的补块期）：读取任务连续补块，前台第一次调用要等到补块间隙，一步可能超过 100 ms（R5.4 的翻页注入钩子就是为了量它）。
- 定位符失效退回按路径打开（大目录末尾）：一次打开 1.4 至 2.1 s，一步就是 2 s（R5.5，C 版预期 WARN）。

**如果严格改成“每步一次调用、跨步持有文件”（ARC-27 允许前台应用跨步持有，最多 2 个）要改的地方**：

1. `catalog_track_text_read()`（`APP/apps/music/catalog/catalog_track_text.c`）要从“一次读完”拆成可恢复的小步状态机：打开、取长度、读第 N 块、解析、关闭各是一步；纯函数 `catalog_track_text()`（`catalog_text.c`）现在靠“按偏移读”回调同步取数据，要改成“先给出读取计划（下一次读哪个偏移、多少字节），由调用者读完再喂回来”的可恢复形式，工作缓冲和进度放静态存储（`.apps_sdram`）。
2. 前台流程（`music_foreground.c`）要持有“正在读的那一行”的状态（打开的 `Service_Filesystem_FileHandleTypeDef`、当前读到第几块、曲名还是窗口行、位置与代次），`music_file_call_used` 的含义改成“本步已做一次 Service 调用”，命中缓存的行仍可连拼；曲名需求优先时要决定是否打断正在读的窗口行（要么关掉重来，要么跨步等它读完）。
3. 失败与终止路径：跨步持有的文件在卷事件、代次变化、新需求覆盖、读取错误时必须关闭（现在“读完即关”所以不需要这套清理），`SERVICE_BUSY` 和卷不可用要落到状态机的每一步；ARC-27 的“最多 2 个”和 SD 4 个文件槽核算（音频流 1 + 前台 1）要重新核对，持有期间读取任务的让路与卷任务的对账会和前台交错得更细。
4. 诊断与计时：`catalog_track_diag_*()` 的单行耗时、`OpenUs`、窗口汇总现在在一步内算完，改成跨步要累加；`Tests/music_catalog`、`Tests/music_foreground`（假 `catalog_track_text_read()` 要变成多步）和模拟器的 `fakes/music_sim.c` 都要跟着改。
5. 代价：同一行的总耗时变长（每步之间多一次调度与别的任务插入），但每步更短、GUI 与前台的时间片更均匀；是否值得由 R5.6 的 step 最长值和 R5.1 的显示延迟决定。

## 给后面的票

- **#219（可见变化，已完成）**：模拟器假播放后端（`Tools/gui_simulator/fakes/playback_sim.c`）的无卡开播从约 100 ms 改成当圈按 NOT_READY 停止，STALE、NO_TRACK 仍约 100 ms 后应答；重录了 `music_card_removed/no_card_play_flash` 与 `vinyl_rotation/cleared_play_not_ready` 两帧（两个场景各一帧，用户 2026-11-07 批准后者）。前台一次切（#218）本身没动 42 个场景的 `.expected`，哈希门槛是可比较的基线。
- **#220（后台任务搬扫描和回收）**：Storage Task 现在只剩曲库扫描和 Flash 回收，优先级 1；扫描搬走之后曲库发布的位置会变，`catalog.c` 里发布成功后的 `foreground_task_wake()` 要保留（前台靠它重发窗口和重答曲名）；注册表要加 `background_step` 列；Flash 压力钩子的 `BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM` 改 0。
- **加应用**：只在 `app_table.c` 多一行（`name`、`boot_init`、`gui_init`、`gui_step`、`foreground_step`）；前台任务的循环、栈和 Monitor 登记都不改。每个应用的前台 step 照 ARC-26 控制在一次文件调用加约 1 ms CPU 之内，最长耗时会出现在 `FOREGROUND:` 行里。

## 命名

任务入口 `foreground_task()`，通知枚举 `FOREGROUND_NOTIFY_*`，配置宏 `FOREGROUND_*`（`foreground_task_config.h`，其他模块经 `foreground_task.h` 取用，不直接包含 config）。
