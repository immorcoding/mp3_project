# 后台任务

后台任务（Background Task）每圈把注册的后台作业各调一次，取最小的返回值当睡眠时长，任何通知都提前叫醒。依据 [ADR-0023](../../../docs/adr/0023-task-split-and-app-contract.md)、规则 ARC-22；决定来源 [#196](https://github.com/immorcoding/mp3_project/issues/196)（作业模型）、[#198](https://github.com/immorcoding/mp3_project/issues/198)、[#201](https://github.com/immorcoding/mp3_project/issues/201)（证据出口与测试钩子放哪张票）；实施票 [#213](https://github.com/immorcoding/mp3_project/issues/213)。

**这张票只落地循环和 Flash 压力钩子。** 曲库扫描和 Flash 回收还在 Storage Task，搬到这里是 #220 的事；曲库扫描也还没改成单步。默认构建里作业表没有作业，后台任务一直阻塞在通知上，不占 CPU。

## 目录

| 路径 | 内容 | 主机测试 |
|---|---|---|
| `background_task.c/.h` | 静态创建（`background_task_create()`）、任务入口 `for(;;) background_loop_step()`；`background_task_wake()` 的声明 | 否（只含任务 API，上板核对） |
| `background_loop.c/.h` | 一圈的判断：`background_loop_run_jobs()`（每个作业一次、取最小）、`background_loop_wait_ticks()`、`background_loop_step()`（调一遍再等通知）、`background_loop_bind()`、`background_task_wake()` 的实现 | 是 |
| `background_jobs.c/.h` | 作业表（`background_jobs_table()`）与各钩子的启动日志（`background_jobs_announce()`）。新增一个作业只在这里多一行 | 是（随压力钩子的测试） |
| `background_flash_pressure.c/.h` | Flash 压力钩子作业 | 是 |
| `background_task_config.h` | 通知位、`BACKGROUND_WAIT_FOREVER`、压力钩子的开关与调参 | — |

## 作业模型（ARC-22）

- 作业是单步函数 `uint32_t step(void)`：每次做一小步（一次可能耗时的文件调用，或约 10 ms 的 CPU 计算），返回多少毫秒后再叫它；`0` 马上，`BACKGROUND_WAIT_FOREVER`（`UINT32_MAX`）等通知。
- 每圈每个作业**恰好一次**，按表的顺序；一个作业返回 0 也不跳过后面的作业。睡眠时长是各返回值的最小值；表里没有作业（或全是空项）是等通知。空项（`NULL`）被跳过，字段留空即“不用”。
- **没有队列、取消接口和作业优先级。** “提交作业”就是应用改自己的状态，再调 `background_task_wake()`；作业每步先看现状，需求变了就放弃手上的活。
- `background_task_wake()` 是 APP 内部接口（#202 M 节授权），不是 Service 接口：任何任务上下文可调，不得从 ISR 调。落在后台任务的通知槽 `BACKGROUND_NOTIFY_EVENT`（0）、标志位 `BACKGROUND_NOTIFY_FLAG_WAKE`，用 `eSetBits`，连叫几次合并成一次醒。作业运行期间到达的通知留在槽里，下一次等待立刻返回，不会丢。任务入口还没记下自己的句柄时调用是空操作（第一圈本来就调所有作业）。
- `INCLUDE_vTaskSuspend` 为 0，`portMAX_DELAY` 在内核里只是约 49.7 天的有限超时；到期后任务多跑一圈，无害。

## 任务参数与优先级

| 项 | 值 |
|---|---|
| 名字 | `Background Task`（Monitor 的栈登记表里有这一行，CPU 行的短键是 `back`） |
| 栈 | 1536 word（6 KiB），`xTaskCreateStatic`，栈在 AXI SRAM 新段 `.app_axi_ram`，TCB 在 DTCM `.bss` |
| 优先级 | 1（`APP_BACKGROUND_TASK_PRIORITY`），与 GUI、Log、Monitor 同级，低于 Storage 与 Flash 启动任务（2） |
| 创建 | Boot 任务在 GUI 任务之后创建，不进文件接口的拒绝名单（后台作业要调文件接口） |

- **栈用 `.app_axi_ram`（NOLOAD，不清零）**：栈由 `xTaskCreateStatic` 整块填成已知值，首次使用前因此完整写入（HWD-2）。`background_task.c` 里的 `_Static_assert((configCHECK_FOR_STACK_OVERFLOW > 1) || (configUSE_TRACE_FACILITY == 1))` 在配置让这个前提失效时编译期报错（与卷任务、Flash 启动任务同一做法）。
- **`.app_axi_ram` 总量**：卷任务 4096 B + Flash 启动任务 4096 B + 后台任务 6144 B = 14336 B（0x3800）。Debug 构建的 map 核对过：`.app_axi_ram 0x2404b600 0x3800`，`app_tasks.c.obj` 0x1000、`background_task.c.obj` 0x1800、`volume_flash_task.c.obj` 0x1000，没有别的贡献者。`RAM`（AXI）区总用量 323072 B（61.62 %）。后面的票往这个段加栈和格子时，用这个数字对账。
- **和 GUI 睡眠（#205）的关系**：GUI 每圈结束后 `vTaskDelay` 睡到下一个 LVGL 定时器到期；同级的后台任务只在 GUI 睡着时轮得到，优先级 0 的 IDLE 在两者都阻塞时才跑。唱盘页上 GUI 几乎不睡（#191 实测空闲约 13 %），后台任务只分得 7 至 13 % 的 CPU，作业慢是预期的，不设饥饿兜底（ARC-15）；非唱盘页 GUI 的平均睡眠 15 ms 左右，后台任务有大量空隙。
- **和 Flash 启动任务（优先级 2）的关系**：Flash 启动任务在 `MountFlash`（3.3 至 8.3 s）里持 Flash 卷锁，优先级高于后台任务，两者同时就绪时它先跑（它大部分时间在等 QSPI 完成，不是一直占着 CPU）；而后台作业在 Flash 卷就绪（卷事件已发）之前**一律不碰 Flash 卷**（`volume_status(VOLUME_ID_FLASH).Mounted` 为假就只轮询），所以两者不会争 Flash 卷锁，也不会有后台作业被 `MountFlash` 的锁挡住 8 s。Flash 卷就绪之后启动任务自删。
- 后台作业持锁时的优先级反转：优先级 1 的后台任务持 Flash 卷锁时，Storage Task（2）要用 Flash 卷（回收）会等它，经优先级继承临时升到 2，反转上界是那一次调用的持锁时间（见下“持锁”）；SD 卷锁与此无关。

## 启动

Boot 任务在创建 GUI 任务之后静态创建后台任务（优先级 1 高于 Boot，创建后立即运行到第一次阻塞）。任务入口先记下自己的句柄（`background_loop_bind`），再让各钩子投启动日志（`background_jobs_announce`，默认构建什么都不投），然后进循环。

## Flash 压力钩子（DIAG-7；清单 1 C2、C3；R4.1、R6.2 至 R6.4）

**用途**：产品固件没有办法把 Flash 空闲块压到回收水位以下，所以真擦除、回收、窄表锁并发、满额删除、Flash 持锁对 SD 和音频读取有没有影响，都没法在板上看到。钩子是后台任务里注册的一个作业，写入、回收、删除，日志带空闲块数和真擦除次数。**默认关闭，发布构建不开。**

### 怎么开

```powershell
cmake --preset firmware-debug -DMP3_HOOK_FLASH_PRESSURE=1
cmake --build --preset firmware-debug
```

缓存变量会留在构建目录里；关掉要显式 `-DMP3_HOOK_FLASH_PRESSURE=0`。只允许 Debug，Release 配置 `MP3_HOOK_FLASH_PRESSURE=1` 会在 configure 时报错。代码写成 `if (BACKGROUND_HOOK_FLASH_PRESSURE != 0U)`，默认构建也编译；默认构建里作业表这一行是空项，链接器丢掉钩子的函数和状态，不占代码和 RAM（核对过：默认构建的 ELF 里没有 `background_flash_pressure_step`；打开后钩子多占约 64 B 的 DTCM 静态数据）。

### 日志（串口里标签后面有冒号）

| 行 | 等级 | 格式 | 何时 |
|---|---|---|---|
| 启动 | WARN | `HOOK: flash_pressure ON chunk=32768 file=4194304 pace=100ms floor=0 reclaim=1 log=10000ms` | 后台任务入口一次；**没有这一行就是没开**，调参值也在里面 |
| 统计 | INFO | `FLASHHOOK: <start|run> free=<空闲块> erases=<真擦除> wr_kib=<已写 KiB> files=<已删文件数> wmax=<ms>ms rmax=<ms>ms dmax=<ms>ms` | Flash 卷就绪后第一步一行（`start`），之后每 10 s 一行（`run`） |
| 卷满 | WARN | `FLASHHOOK: flash full after <字节> B, closing file` | 写入返回的字节数不足或 `NO_SPACE`，每次会话一次 |
| 停下 | ERROR | `FLASHHOOK: stopped at=<wait|cleanup|open|write|reclaim|close|remove> status=<Service 状态数值> until Flash volume sequence changes` | 任何非“卷不可用”的失败，一次 |

- `free` 是 `Platform_Flash_GetVolumeDiagnostics()` 的 `FreeBlocks`（已确认擦净的空闲块），`erases` 是 `SessionErases` 减去钩子开始时的读数，也就是**钩子开始之后的真擦除次数**（含写入时 FTL 自己前台回收擦的，不含格式化和钩子之前的）。`wmax`、`rmax`、`dmax` 是自上一行起最长一次写、回收、删除的耗时（含等 Flash 卷锁的时间），清零重算；空闲块接近耗尽时 `wmax` 会到 1.5 至 3 s（#191 实测写 64 KiB 平均 2.3 s、最长 3.0 s）。
- 读诊断不拿 Flash 卷锁，只给人看：别的任务正在操作 FTL 时读到的值可能差一两个。`FREE_FLOOR` 是唯一用它做判断的地方，差一两个无妨。FTL 没绑定时读不到，统计行不投，作业照常。
- **节拍与消息池（DIAG-3）**：统计行每 10 s 一行（`BACKGROUND_HOOK_FLASH_PRESSURE_LOG_PERIOD_MS`），`Monitor` 5 s 一批，二者同时到时最坏占 1 块。核算见下“消息池”。

### 作业做什么（状态机，每步最多一次文件调用）

```
等卷 → 删上次遗留 → 新建 FLASHP.TMP → [写 32 KiB → 回收一次]×128 → 关闭 → 删除 → 新建 …
```

| 步 | 调用 | 返回（ms 后再叫） |
|---|---|---|
| 等卷 | 无（只读卷序号） | 500；Flash 卷挂载之后才继续 |
| 删遗留 | `RemoveFile` | 0（文件不在也无所谓，只看卷是否可用） |
| 新建 | `OpenFile(CREATE_NEW)` | 0；遇 `SERVICE_BUSY` 先删遗留，10 ms 后重试，最多 3 次，仍失败就停 |
| 写 | `WriteFile` 32 KiB | 0（紧接着回收）；关掉本作业的回收时是 100 |
| 回收 | `ReclaimFlash` | 100（节拍，也是回收的节拍：与产品回收 100 ms 一次一致） |
| 关闭 | `CloseFile` | 0 |
| 删除 | `RemoveFile` | 100 |

- **Flash 卷还没就绪（含 Flash 启动任务挂载期间）**：作业只读 `volume_status(VOLUME_ID_FLASH)`（不拿锁），没挂载就返回 500 ms 回头再看，**不调任何文件接口**，所以不会被 `MountFlash` 持的 Flash 卷锁（3.3 至 8.3 s）挡住，也不会往还没就绪的卷里写。卷就绪后下一圈（至多 500 ms）开始。Flash 卷消失或卷序号变化（卸载再挂载）时，旧文件句柄已随卸载作废，作业**不去关它**，回到“删遗留”重新开始；任何调用返回 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 都当“卷不可用”处理，不是故障、不记 ERROR。
- **压力文件放哪**：Flash 卷根目录 `FLASHP.TMP`（8.3 名，没有目录依赖）。**什么时候删**：每个文件写满 4 MiB（`FILE_BYTES`）就关闭、删除，再新建；开机后第一次挂载先删上次遗留（测试中途断电或停下时留下的）。FTL 没有 TRIM：删掉文件不会把块还成空闲块，空闲块只靠“写新数据时旧块变失效、回收擦掉”回来，所以反复写删是压低空闲块的正确办法，删除本身也是被测的持锁操作（4 MiB 约 0.55 s，推算自 138 ms/MiB）。**测完要不要手动清**：钩子关闭的固件不会删它；中途停下时最多留下一个不到 4 MiB 的 `FLASHP.TMP`，下次开着钩子的固件挂载时自动删；产品固件不用 Flash 卷放别的东西，留着无碍。
- **Flash 满时怎么办**：卷满（`WriteFile` 写进去的字节数少于要求的，或返回 `NO_SPACE`）**不是故障**：记一条 WARN（每次会话一次），把这个文件当写完，关闭、删除，再新建。FatFs 的 `f_write` 在盘满时返回 OK 加较少的字节数，所以主要靠字节数判断。新建时目录满或无空间（`NO_SPACE`）是故障，直接停下。Flash 卷的逻辑容量约 18.9 MiB（24 MiB 物理，10 % 预留），压力文件 4 MiB 远小于它，正常情况下碰不到满。想测 16 MiB 满额删除（R6.4）把 `FILE_BYTES` 调到 16 MiB（要重编）。
- **失败一次就停**：除“卷不可用”外的任何失败（写、回收、新建、关闭、删除）记一条 ERROR，文件还开着先关掉（不删除），之后只每 500 ms 看一次卷序号，序号变了才重新开始（与 #202 F 节“回收失败一次就停，到 Flash 卷序号变化为止”同一规则）。钩子只做压力，不做恢复。
- **每圈的写入量与节拍（C2 要能在合理时间内压到水位以下）**：每步写 32 KiB（单次写上限），回收一次，再让出 100 ms。空闲块充足时写 32 KiB 约 0.17 至 0.37 s（#191：64 KiB 写 330–738 ms，平均约 515 ms），一圈约 0.3 至 0.5 s，约 65 至 110 KB/s。一次 32 KiB 写覆盖 10 个逻辑组，消耗 10 个空闲块（每组 3584 B）。本板 FTL 物理 24 MiB，6142 个数据块，回收水位 `low` = 462、停止水位 554（预留 615 块的 75 % 与 90 %）。**从全新格式化的卷**压到水位以下需要写掉约 5700 个块、约 570 圈，估算 3.5 至 5 分钟（估算，未测）；如果上一轮测试已经把空闲块压低，`start` 行的 `free=` 一开始就接近或低于水位，立刻进入真擦除。C2 的 30 分钟远够用。水位以下之后每写一块要靠回收和写入自己的前台回收腾位置，空闲块会一路降到 FTL 的下限 2 附近，写入变成 1.5 至 3 s 一次（#191 实测），这正是 R6.4 要看的“空闲块接近耗尽时写 32 KiB”和 R4.1“Flash 写入中切歌”的最坏情形。
- **不想耗尽、只想压在水位附近**（R6.2 的“先压到水位以下，确认有真擦除”）：把 `BACKGROUND_HOOK_FLASH_PRESSURE_FREE_FLOOR` 设成比水位小的数（例如 300，要重编）。空闲块低于它时作业暂停写入，只靠回收（每步一次调用）把空闲块抬回来，回到下限以上再写；默认 0 不设下限。
- **回收由谁调**：钩子自己每写一块调一次回收（`BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM = 1`）。原因：现在的 Storage Task 有打开的音频流时不回收（STOR-12），播放中就没有真擦除，C3“播放时回收且欠载为 0”看不到；#220 把回收做成后台任务自己的作业、放宽 STOR-12 之后，**把这个常量改成 0**，否则回收被叫两次，擦除节拍比产品高一倍。

### 持锁（R4.2、R6.3、R6.4 要看的）

作业只拿 **Flash 卷锁**和 Flash 的窄表锁，不碰 SD 卷锁；SD 卷与 Flash 卷各一把锁（ARC-21），所以 Flash 的长持锁不挡 SD 读，也不挡音频读取，这正是 R4 要证明的。单次调用持 Flash 卷锁的最长时间：

| 调用 | Flash 卷锁 | 依据 |
|---|---|---|
| `WriteFile` 32 KiB，空闲块充足 | 约 0.17 至 0.37 s | #191：64 KiB 330–738 ms 折半 |
| `WriteFile` 32 KiB，空闲块接近耗尽 | 约 1.5 s，最长约 3 s | #191：64 KiB 平均 2.3 s、最长 3.0 s 折半或原样 |
| `RemoveFile` 4 MiB | 约 0.55 s | 138 ms/MiB（推算），表锁另算 |
| `OpenFile(CREATE_NEW)` | 最长 10.1 ms（Flash 锁加表锁） | 持锁上界表 |
| `CloseFile`（含同步） | 24–138 ms | #191 |
| `ReclaimFlash` | 水位以上约 14 µs；真擦除一块 38–50 ms | #191 |

Storage Task 现在会为 Flash 回收等这把锁（只在没有音频流时），音频流在读时不碰 Flash 卷；#215 把音频读取拆成独立任务后它也不碰 Flash 卷。**对 SD 卷与音频读取的影响应为零**——主机测试只能证明作业不调 SD 的接口，真实的“互不影响”靠第 4 次上板（R4.1、R4.2）。

### 消息池（DIAG-3；Service Log 30 块，Log Task 每 5 ms 转交一条）

稳态任务从 8 个变 9 个（加 `Background Task`）：Monitor 一批稳态 14 行，开机后 `MountFlash` 期间多一个 Flash 启动任务 15 行（`MONITOR_CPU_TASK_MAX` 16 个任务加 5 行固定行共 21 行的上界不变，`monitor_task.c` 的 `_Static_assert` 照旧）。**默认构建最坏同时到达**：Monitor 15 + Filesystem 持锁统计 3 + 曲库短锁 1 + 拔卡对账 5 = **24 行，留 6 块**（#212 核算的 23 行再加 1，来自后台任务这一行）。**钩子打开的构建**再加统计行 1 行 = 25 行，留 5 块；停下（ERROR）和卷满（WARN）各偶发一条，同时到达最坏 27 行，留 3 块。排空约 125 至 135 ms。钩子的统计行节拍 10 s，不是每步一条；每行正文不超过 127 字符（主机测试在极端取值下核对）。

### 主机测试证明什么、不证明什么（HAR-5）

`Tests/background_task`（28 个 CTest，见该目录 README）：证明作业每圈各一次、最小等待、通知提前醒、作业运行期间到的通知不丢、钩子的步序与返回值、卷没就绪时不调文件接口、各类失败的归类（卷不可用重新开始 / 其他失败一次就停 / 卷满当写完）、新建 BUSY 的重试上限、空闲块下限暂停写入、日志格式与节拍、默认构建里表是空项。**不证明**：真实任务切换与优先级继承、`xTaskNotifyWaitIndexed` 在真实内核里的睡眠与醒来延迟、与 GUI 同级时的 CPU 份额、真实的 Flash 持锁时长与擦除、SD 与 Flash 的并发是否互不影响、任务栈余量（R6.8，Monitor 栈行）、`background_task.c` 的静态创建。这些靠第 4 次上板（钩子）和第 6 次上板。

## 给后面票的注意事项

- **#215（音频读取，第 4 次上板用这个钩子）**：见 inbox 与汇报；要点：用 `-DMP3_HOOK_FLASH_PRESSURE=1` 的 Debug 构建，开机日志找 `HOOK: flash_pressure ON`，约 4 分钟内 `free` 降到 462 以下且 `erases` > 0；R4.1“Flash 写入中切歌”要在 `wmax` 大的时候切。
- **#220（扫描与回收搬进来）**：在 `background_jobs.c` 的表里各加一行；回收做成内置作业后把 `BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM` 改 0。扫描的 `SERVICE_BUSY` 重试现在用 `vTaskDelay`，单步里要换成“返回 10 ms 后再叫我”。栈共用 6 KiB，遍历状态放静态存储。
