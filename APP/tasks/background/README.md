# 后台任务

后台任务（Background Task）每圈把注册的后台作业各调一次，取最小的返回值当睡眠时长，任何通知都提前叫醒。依据 [ADR-0023](../../../docs/adr/0023-task-split-and-app-contract.md)、规则 ARC-22；决定来源 [#196](https://github.com/immorcoding/mp3_project/issues/196)（作业模型）、[#198](https://github.com/immorcoding/mp3_project/issues/198)、[#201](https://github.com/immorcoding/mp3_project/issues/201)（证据出口与测试钩子放哪张票）；实施票 [#213](https://github.com/immorcoding/mp3_project/issues/213)（循环和 Flash 压力钩子）、[#220](https://github.com/immorcoding/mp3_project/issues/220)（搬进曲库扫描和 Flash 回收，删除 Storage Task）。

**现在作业表里有三样**：Flash 回收（内置，不进注册表，见下“回收作业”）、默认关闭的 Flash 压力钩子（内置）、音乐的曲库扫描（注册表 `background_step`，见 [曲库](../../apps/music/catalog/README.md)“扫描单步与错误分类”）。回收和扫描原来在 Storage Task 里，**Storage Task、`Storage_StatusTypeDef` 和 `APP/tasks/storage/` 目录已随 #220 删除**，它的 README 拆到了各任务：卷事件的订阅与失败约定在 [卷任务](../volume/README.md)、[音频读取任务](../audio_reader/README.md)、[曲库](../../apps/music/catalog/README.md)和本页，优先级的排序依据在 [任务与优先级](../README.md)。

## 目录

| 路径 | 内容 | 主机测试 |
|---|---|---|
| `background_task.c/.h` | 静态创建（`background_task_create()`）、任务入口：拼作业表、登记卷事件订阅、`for(;;) background_loop_step()`；`background_task_wake()` 的声明 | 否（只含任务 API，上板核对） |
| `background_loop.c/.h` | 一圈的判断：`background_loop_run_jobs()`（每个作业一次、取最小）、`background_loop_wait_ticks()`、`background_loop_step()`（调一遍再等通知）、`background_loop_bind()`、`background_task_wake()` 的实现 | 是 |
| `background_jobs.c/.h` | 拼作业表 `background_jobs_build()`：内置作业（回收、压力钩子）在前，注册表各应用的 `background_step`（空字段跳过）在后；各钩子的启动日志 `background_jobs_announce()` | 是（随压力钩子的测试） |
| `background_reclaim.c/.h` | Flash 回收作业（STOR-12）：100 ms 周期、一次一块、失败一次就停、卷序号为 0 直接返回 | 是（`background_reclaim_tests`） |
| `background_flash_pressure.c/.h` | Flash 压力钩子作业 | 是 |
| `background_task_config.h` | 通知位（叫醒、卷事件）、`BACKGROUND_WAIT_FOREVER`、作业表上限、回收周期、压力钩子的开关与调参 | — |

## 作业模型（ARC-22）

- 作业是单步函数 `uint32_t step(void)`：每次做一小步（一次可能耗时的文件调用，或约 10 ms 的 CPU 计算），返回多少毫秒后再叫它；`0` 马上，`BACKGROUND_WAIT_FOREVER`（`UINT32_MAX`）等通知。
- 每圈每个作业**恰好一次**，按表的顺序；一个作业返回 0 也不跳过后面的作业。睡眠时长是各返回值的最小值；表里没有作业（或全是空项）是等通知。空项（`NULL`）被跳过，字段留空即“不用”。**表在任务入口拼一次**（`background_jobs_build()`，`BACKGROUND_JOB_MAX` = 8 项，放不下断言停机）：回收、压力钩子（关闭时没有这一项）、各应用的 `background_step`。新增应用的后台作业只在注册表 `APP/apps/app_table.c` 多一行，不改这里。
- **没有队列、取消接口和作业优先级。** “提交作业”就是应用改自己的状态，再调 `background_task_wake()`；作业每步先看现状，需求变了就放弃手上的活。有两个后台作业的应用自己在 `background_step` 里合并。
- **叫醒后台任务的有两样**，都落在同一个通知槽 `BACKGROUND_NOTIFY_EVENT`（0）里、用 `eSetBits`（连叫几次合并成一次醒；作业运行期间到达的通知留在槽里，下一次等待立刻返回，不会丢）：`background_task_wake()`（标志位 `BACKGROUND_NOTIFY_FLAG_WAKE`，APP 内部接口，#202 M 节授权，任何任务上下文可调，不得从 ISR 调；任务入口还没记下自己的句柄时是空操作，第一圈本来就调所有作业）；**卷事件**（标志位 `BACKGROUND_NOTIFY_FLAG_VOLUME`，任务入口登记订阅：SD 卷就绪或消失叫醒扫描，Flash 卷就绪叫醒回收）。FLAG 只用来叫醒，醒来后各作业自己比对卷序号。
- **卷事件订阅者现在是三个**：音频读取、前台、后台（`VOLUME_SUBSCRIBER_MAX` 为 4，余一个）；Storage Task 删除后不再占一个。
- `INCLUDE_vTaskSuspend` 为 0，`portMAX_DELAY` 在内核里只是约 49.7 天的有限超时；到期后任务多跑一圈，无害。

## 任务参数与优先级

| 项 | 值 |
|---|---|
| 名字 | `Background Task`（Monitor 的栈登记表里有这一行，CPU 行的短键是 `back`） |
| 栈 | 1536 word（6 KiB），`xTaskCreateStatic`，栈在 AXI SRAM 新段 `.app_axi_ram`，TCB 在 DTCM `.bss`；`#220` 没有改大（作业表 32 B 放在任务栈上，扫描的遍历状态在 `.apps_sdram`，不占栈） |
| 优先级 | 1（`APP_BACKGROUND_TASK_PRIORITY`），**与 Flash 启动任务同级**（#220：Flash 启动任务由 2 降到 1，见下），低于前台、GUI、Log、Monitor（2） |
| 创建 | Boot 任务在 GUI 任务之后创建，不进文件接口的拒绝名单（后台作业要调文件接口） |

- **栈用 `.app_axi_ram`（NOLOAD，不清零）**：栈由 `xTaskCreateStatic` 整块填成已知值，首次使用前因此完整写入（HWD-2）。`background_task.c` 里的 `_Static_assert((configCHECK_FOR_STACK_OVERFLOW > 1) || (configUSE_TRACE_FACILITY == 1))` 在配置让这个前提失效时编译期报错（与卷任务、Flash 启动任务同一做法）。
- **`.app_axi_ram` 总量**不变：0x9380 = 37760 B（Debug 与 Release 的 map 核对过，起点 `0x2404b600`，链接脚本没改）：五个任务栈 24 KiB（卷 4、Flash 启动 4、音频读取 4、后台 6、前台 6）加界面数据格子与 GUI 副本。后台任务栈若要加大也放这个段、不改链接脚本。
- **和 GUI 睡眠（#205）的关系**：GUI 每圈结束后 `vTaskDelay` 睡到下一个 LVGL 定时器到期；后台任务只在 GUI、前台、Log、Monitor 都睡着时轮得到，优先级 0 的 IDLE 在两者都阻塞时才跑。唱盘页上 GUI 几乎不睡（#191 实测空闲约 13 %），后台任务只分得 7 至 13 % 的 CPU，作业慢是预期的，不设饥饿兜底（ARC-15）；非唱盘页 GUI 的平均睡眠 15 ms 左右，后台任务有大量空隙。扫描进行中返回 0，后台任务不阻塞，会一直占着它分到的时间（IDLE 在扫描期间跑不到）；扫完回到无限等待。
- **扫描进行中后台任务不让出（#220，用户决定保留）**：扫描作业每一步返回 0，后台任务因此不阻塞，一直占着它分到的时间，直到扫完回到无限等待。后果和怎么读：① **IDLE（优先级 0）在这几秒跑不到**，Monitor CPU 行的 `idle=` 在开机扫描那一批读 **0**（开机前约 8 s 后台任务和 Flash 启动任务同级轮转，前台、GUI 睡着时它俩把 CPU 占满），不是故障；扫完回到正常。② **`Create Task`（Boot）自删后的 TCB 与 768 B 栈由 IDLE 回收**，IDLE 跑不到的这几秒里它们还没还给堆：Monitor `Heap` 行的“历史最小余量”可能比稳态低约 0.9 KiB，读 `Heap` 的最低值时要知道这一点（Flash 启动任务是静态的，没有堆回收问题）。③ 作业不阻塞也意味着每圈各作业都被调一次（回收作业只是比对时间，几乎不占时间）。第 6 次上板 R6.11、R6.6 顺带看：开机批的 `idle=`、`flas=`、`back=` 与 `Heap` 行，记录，不当成异常。
- **和 Flash 启动任务（优先级 1，#220）的关系**：Flash 启动任务在 `MountFlash`（3.3 至 8.3 s）里持 Flash 卷锁，与后台任务同级，同时就绪时按 1 ms 时间片轮转；后台作业在 Flash 卷就绪（卷事件已发）之前**一律不碰 Flash 卷**（回收看 `volume_status(VOLUME_ID_FLASH)`，序号为 0 或没挂载就不调），所以两者不会争 Flash 卷锁，也不会有后台作业被 `MountFlash` 的锁挡住 8 s。Flash 卷就绪之后启动任务自删。
- **为什么 Flash 启动任务降到 1（#220，用户 2026-11-07 选的方案 A）**：第 5 次上板实测，大目录 12000 首开机扫描 9.0–9.1 s（第 4 次上板 3.6 s），常规库 24 首 0.39–0.87 s（以前约 24 ms）。原因是 Storage 按 spec 降到优先级 1 之后，开机前约 8 s Flash 启动任务（优先级 2）占约 77 % CPU、GUI 约 20 %，优先级 1 的扫描几乎轮不上；持锁的扫描被抢占，SD 卷锁一次 `ReadDir` 持有 820 ms。扫描搬进后台任务（同为优先级 1）问题会原样保留，所以把 Flash 启动任务降到 1，与后台任务同级轮转。**总 CPU 工作量不变，只是换了顺序**：扫描先做完，Flash 就绪晚大约“扫描占用的 CPU 时间”。**推算**（调度模型，按第 4、5 次上板粗校，不是实测）：大目录开机扫描约 3.2–4.3 s，常规库约 0.1 s；Flash 就绪约 10 s（原约 8 s，大目录晚约 1.5–2 s，常规库几乎没有）。附带：持锁的扫描不会再被 Flash 任务抢占（800 ms 级的 `ReadDir` 持有应消失），GUI 也不再和它平分时间。**代价**：开机后立刻播放并停在唱盘页（空闲约 13 %）时，Flash 就绪会从约 15 s 推到约 35 s（粗模型）；0.10.0 里没有东西要求 Flash 早就绪（只有回收和钩子，开机时空闲块充足），以后“开机读设置”之类的版本要重新评估。**不改公开接口**；改的是 `APP/app_config.h` 的 `APP_FLASH_BOOT_TASK_PRIORITY`，spec A 表“Flash 启动 2”随之变成 1（记在 inbox）。`Tests/background_task` 的 `test_task_priorities.c` 用编译期断言钉住排序。实测不理想再退到“扫描期间临时升到 2、只升开机第一次扫描”，那要再问用户。第 6 次上板量开机扫描时长（常规库、大目录各一次），见 [板测清单](../../../docs/agents/board-test-checklist.md)第 6 次。
- 后台作业持锁时的优先级反转：优先级 1 的后台任务持卷锁时，前台任务（2）、音频读取任务（3）要用同一卷会等它，经优先级继承临时升到 2 或 3，反转上界是那一次调用的持锁时间（见下“持锁”）；扫描每步一次目录调用、每条一次锁，所以每次持锁只是一条目录项的时间。

## 启动

Boot 任务在创建 GUI 任务之后静态创建后台任务（优先级 1 高于 Boot，创建后立即运行）。任务入口：拼作业表（`background_jobs_build`）→ 记下自己的句柄（`background_loop_bind`）→ 登记卷事件订阅（`volume_subscribe`，登记后第一圈不等通知，先把所有作业各调一遍，所以晚于第一个卷事件启动也不会漏）→ 各钩子投启动日志（`background_jobs_announce`，默认构建什么都不投）→ 进循环。第一圈里 SD 卷序号已非 0 就开始扫描，Flash 卷序号为 0 回收作业直接返回。

## 回收作业（STOR-12、#196 第 18、19 条、#220）

`background_reclaim_step()`：Flash 卷就绪之后每 `BACKGROUND_RECLAIM_PERIOD_MS`（100 ms，从上一次调用开始算）给 FTL 一次回收机会，**一次最多一块**（`Service_Filesystem_ReclaimFlash()` 自己一次最多回收一块）；是否真的擦除由 FTL 决定（空闲块高于水位约 14 µs 就返回，低于水位才真擦除约 40 至 50 ms，只挡 Flash 卷，#191）。**不进注册表**，是后台任务的内置作业。

- **播放和暂停时也提供机会**（STOR-12 现行文字：打开的音频流不影响回收机会）：回收只拿 Flash 卷锁，音频读取任务只拿 SD 卷锁，回收和读块在两个任务里、两把锁上，擦除不再挡补块。前提是每卷一把锁已落地，且运行中没有任务读 QSPI 映射窗口（CP936 表在 SDRAM，#188 查过），新增这类读者要重新评估。`audio_reader` 不再有“流是否打开”的查询。
- **Flash 卷序号为 0 直接返回**（开机至今没发过 Flash 卷事件：Flash 启动任务还在 `MountFlash`，或挂载失败）：不碰 Flash 卷，返回 `BACKGROUND_WAIT_FOREVER`，等卷事件叫醒；卷没挂载同样。第一次回收机会在看到新序号之后一个周期。
- **失败一次就停**：`ReclaimFlash` 返回 `SERVICE_ERROR` 或 `SERVICE_TIMEOUT` 记一条 ERROR，之后作业不再调用，到 Flash 卷序号变化为止（FTL 这时要显式恢复才能继续，0.10.0 不做运行中恢复）。原来每 100 ms 重试并记两条 ERROR，会挤占消息池。其他返回值（含 `SERVICE_BUSY`、卷不可用类）不算失败，下个周期照常。
- **日志**：`E (…) FLASH: Reclaim failed status=<Service 状态数值> (stopped until Flash seq changes) <FTL 失败诊断>`（标签 `FLASH`，串口里标签后有冒号；状态数值 1 是 `SERVICE_ERROR`，5 是 `SERVICE_TIMEOUT`）。状态数值只说文件接口报错（FTL 的任何非 OK 结果都压成 1），具体是 `NO_SPACE`、`CORRUPT` 还是 `IO_ERROR` 看后面的诊断片段，格式和读法见下“Flash 失败诊断”。仍是一行、不超过 127 字符，消息池核算不变。
- **只在后台任务访问**，状态（见到的卷序号、上次调用时刻、是否已停）是静态量。`BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM` 默认改成 0：钩子构建里不再由钩子另调一次回收，擦除节拍和产品一致（见下“回收由谁调”）。

## 扫描作业

曲库扫描（`catalog_scan_step()`，音乐注册表的 `background_step`）的状态机、错误表、发布规则和日志见 [曲库 README](../../apps/music/catalog/README.md)“扫描单步与错误分类”。要点：一步最多一次目录调用、一步读一条、每条一次锁、遍历状态在 `.apps_sdram`、每步比对卷序号（变了就关目录并丢弃，卡还在就重新开始）、不向 GUI 发布进度、发布成功后 `foreground_task_wake()` 一次；SD 卷事件叫醒它，扫描进行中返回 0（不睡），`SERVICE_BUSY` 返回 10 ms。



## Flash 压力钩子（DIAG-7；清单 1 C2、C3；R4.1、R6.2 至 R6.4）

**用途**：产品固件没有办法把 Flash 空闲块压到回收水位以下，所以真擦除、回收、窄表锁并发、满额删除、Flash 持锁对 SD 和音频读取有没有影响，都没法在板上看到。钩子是后台任务里注册的一个作业，写入、回收、删除，日志带空闲块数和真擦除次数。**默认关闭，发布构建不开。**

### 怎么开

```powershell
cmake --preset firmware-debug -DMP3_HOOK_FLASH_PRESSURE=1 -DMP3_HOOK_FLASH_PRESSURE_FREE_FLOOR=300 -DMP3_HOOK_FLASH_PRESSURE_FILE_MIB=4
cmake --build --preset firmware-debug
```

两个调参是 CMake 缓存变量（#220 第 6 次上板追加，不用再为每一版固件临时改源码）：`MP3_HOOK_FLASH_PRESSURE_FREE_FLOOR`（空闲块下限，默认 0 不设；300 把空闲块保持在回收水位 462 以下、耗尽以上，擦除全来自回收作业）和 `MP3_HOOK_FLASH_PRESSURE_FILE_MIB`（压力文件大小，1 至 16 的整数 MiB，默认 4）。它们变成编译宏 `BACKGROUND_HOOK_FLASH_PRESSURE_FREE_FLOOR`、`BACKGROUND_HOOK_FLASH_PRESSURE_FILE_BYTES`；启动那行 `HOOK:` 里的 `floor=`、`file=` 就是编进去的值，**板上以它为准**。非默认值只允许 Debug，Release 配置里设会在 configure 时报错。

缓存变量会留在构建目录里；关掉要显式 `-DMP3_HOOK_FLASH_PRESSURE=0`，两个调参回默认要显式写 `-DMP3_HOOK_FLASH_PRESSURE_FREE_FLOOR=0 -DMP3_HOOK_FLASH_PRESSURE_FILE_MIB=4`。只允许 Debug，Release 配置 `MP3_HOOK_FLASH_PRESSURE=1` 会在 configure 时报错。代码写成 `if (BACKGROUND_HOOK_FLASH_PRESSURE != 0U)`，默认构建也编译；默认构建里作业表这一行是空项，链接器丢掉钩子的函数和状态，不占代码和 RAM（核对过：默认构建的 ELF 里没有 `background_flash_pressure_step`；打开后钩子多占约 64 B 的 DTCM 静态数据）。

### 日志（串口里标签后面有冒号）

| 行 | 等级 | 格式 | 何时 |
|---|---|---|---|
| 启动 | WARN | `HOOK: flash_pressure ON chunk=32768 file=4194304 pace=100ms floor=0 reclaim=0 log=10000ms` | 后台任务入口一次；**没有这一行就是没开**，调参值也在里面（`reclaim=0` 是默认：回收由后台任务自己的回收作业负责，#220） |
| 统计 | INFO | `FLASHHOOK: <start|run> free=<空闲块> erases=<真擦除> wr_kib=<已写 KiB> files=<已删文件数> wmax=<ms>ms rmax=<ms>ms dmax=<ms>ms` | Flash 卷就绪后第一步一行（`start`），之后每 10 s 一行（`run`） |
| 卷满 | WARN | `FLASHHOOK: flash full after <字节> B, closing file` | 写入返回的字节数不足或 `NO_SPACE`，每次会话一次 |
| 停下 | ERROR | `FLASHHOOK: stopped at=<wait\|cleanup\|open\|write\|reclaim\|close\|remove> status=<Service 状态数值> <FTL 失败诊断>[ pre=<空闲>/<有效>/<失效> req=<字节> file=<字节>]` | 任何非“卷不可用”的失败，一次（之后到 Flash 卷序号变化为止不再动） |

- `free` 是 `Platform_Flash_GetVolumeDiagnostics()` 的 `FreeBlocks`（已确认擦净的空闲块），`erases` 是 `SessionErases` 减去钩子开始时的读数，也就是**钩子开始之后的真擦除次数**（含写入时 FTL 自己前台回收擦的，不含格式化和钩子之前的）。`wmax`、`rmax`、`dmax` 是自上一行起最长一次写、回收、删除的耗时（含等 Flash 卷锁的时间），清零重算；空闲块接近耗尽时 `wmax` 会到 1.5 至 3 s（#191 实测写 64 KiB 平均 2.3 s、最长 3.0 s）。
- **停下行怎么读**（第 6 次上板追加：原来只有 `status=1`，分不出原因）：`status` 是文件接口的状态，`ftl=…` 起是 Platform 记下的 FTL 最近一次以非 OK 收尾的请求，见下“Flash 失败诊断”。写步失败时多三段：`pre=<空闲>/<有效>/<失效>` 是**失败那次写开始之前**的块数快照（失败后 FTL 不就绪，有效、失效读作 0，所以要事先存），`req=` 是那次写请求的字节数，`file=` 是当前压力文件已写入的字节数（不含这次）。仍是一行、不超过 127 字符，`rej`、`hw` 在最后，放不下先截断它们。
- 读诊断不拿 Flash 卷锁，只给人看：别的任务正在操作 FTL 时读到的值可能差一两个。`FREE_FLOOR` 是唯一用它做判断的地方，差一两个无妨。FTL 没绑定时读不到，统计行不投，作业照常。
- **节拍与消息池（DIAG-3）**：统计行每 10 s 一行（`BACKGROUND_HOOK_FLASH_PRESSURE_LOG_PERIOD_MS`），`Monitor` 5 s 一批，二者同时到时最坏占 1 块。核算见下“消息池”。

### 作业做什么（状态机，每步最多一次文件调用）

```
等卷 → 删上次遗留 → 新建 FLASHP.TMP → [写 32 KiB（→ 回收一次，只在 RECLAIM 打开时）]×128 → 关闭 → 删除 → 新建 …
```

| 步 | 调用 | 返回（ms 后再叫） |
|---|---|---|
| 等卷 | 无（只读卷序号） | 500；Flash 卷挂载之后才继续 |
| 删遗留 | `RemoveFile` | 0（文件不在也无所谓，只看卷是否可用） |
| 新建 | `OpenFile(CREATE_NEW)` | 0；遇 `SERVICE_BUSY` 先删遗留，10 ms 后重试，最多 3 次，仍失败就停 |
| 写 | `WriteFile` 32 KiB | 100（默认，本作业不另调回收，回收由回收作业负责）；`BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM` 打开时是 0（紧接着回收） |
| 回收（只有 `RECLAIM` 打开时） | `ReclaimFlash` | 100（节拍） |
| 关闭 | `CloseFile` | 0 |
| 删除 | `RemoveFile` | 100 |

- **Flash 卷还没就绪（含 Flash 启动任务挂载期间）**：作业只读 `volume_status(VOLUME_ID_FLASH)`（不拿锁），没挂载就返回 500 ms 回头再看，**不调任何文件接口**，所以不会被 `MountFlash` 持的 Flash 卷锁（3.3 至 8.3 s）挡住，也不会往还没就绪的卷里写。卷就绪后下一圈（至多 500 ms）开始。Flash 卷消失或卷序号变化（卸载再挂载）时，旧文件句柄已随卸载作废，作业**不去关它**，回到“删遗留”重新开始；任何调用返回 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 都当“卷不可用”处理，不是故障、不记 ERROR。
- **压力文件放哪**：Flash 卷根目录 `FLASHP.TMP`（8.3 名，没有目录依赖）。**什么时候删**：每个文件写满 4 MiB（`FILE_BYTES`）就关闭、删除，再新建；开机后第一次挂载先删上次遗留（测试中途断电或停下时留下的）。FTL 没有 TRIM：删掉文件不会把块还成空闲块，空闲块只靠“写新数据时旧块变失效、回收擦掉”回来，所以反复写删是压低空闲块的正确办法，删除本身也是被测的持锁操作（4 MiB 约 0.55 s，推算自 138 ms/MiB）。**测完要不要手动清**：钩子关闭的固件不会删它；中途停下时最多留下一个不到 4 MiB 的 `FLASHP.TMP`，下次开着钩子的固件挂载时自动删；产品固件不用 Flash 卷放别的东西，留着无碍。
- **Flash 满时怎么办**：卷满（`WriteFile` 写进去的字节数少于要求的，或返回 `NO_SPACE`）**不是故障**：记一条 WARN（每次会话一次），把这个文件当写完，关闭、删除，再新建。FatFs 的 `f_write` 在盘满时返回 OK 加较少的字节数，所以主要靠字节数判断。新建时目录满或无空间（`NO_SPACE`）是故障，直接停下。Flash 卷的逻辑容量约 18.9 MiB（24 MiB 物理，10 % 预留），压力文件 4 MiB 远小于它，正常情况下碰不到满。想测 16 MiB 满额删除（R6.4）configure 时带 `-DMP3_HOOK_FLASH_PRESSURE_FILE_MIB=16`（要重编，不用改源码）。
- **失败一次就停**：除“卷不可用”外的任何失败（写、回收、新建、关闭、删除）记一条 ERROR，文件还开着先关掉（不删除），之后只每 500 ms 看一次卷序号，序号变了才重新开始（与 #202 F 节“回收失败一次就停，到 Flash 卷序号变化为止”同一规则）。钩子只做压力，不做恢复。
- **每圈的写入量与节拍（C2 要能在合理时间内压到水位以下）**：每步写 32 KiB（单次写上限），再让出 100 ms（默认不另调回收，回收由回收作业每 100 ms 一次；`RECLAIM` 打开时写完紧接着回收一次）。空闲块充足时写 32 KiB 约 0.17 至 0.37 s（#191：64 KiB 写 330–738 ms，平均约 515 ms），一圈约 0.3 至 0.5 s，约 65 至 110 KB/s。一次 32 KiB 写覆盖 10 个逻辑组，消耗 10 个空闲块（每组 3584 B）。本板 FTL 物理 24 MiB，6142 个数据块，回收水位 `low` = 462、停止水位 554（预留 615 块的 75 % 与 90 %）。**从全新格式化的卷**压到水位以下需要写掉约 5700 个块、约 570 圈，估算 3.5 至 5 分钟（估算，未测）；如果上一轮测试已经把空闲块压低，`start` 行的 `free=` 一开始就接近或低于水位，立刻进入真擦除。C2 的 30 分钟远够用。水位以下之后每写一块要靠回收和写入自己的前台回收腾位置，空闲块会一路降到 FTL 的下限 2 附近，写入变成 1.5 至 3 s 一次（#191 实测），这正是 R6.4 要看的“空闲块接近耗尽时写 32 KiB”和 R4.1“Flash 写入中切歌”的最坏情形。
- **不想耗尽、只想压在水位附近**（R6.2 的“先压到水位以下，确认有真擦除”）：configure 时带 `-DMP3_HOOK_FLASH_PRESSURE_FREE_FLOOR=300`（比水位小的数，要重编；对应编译宏 `BACKGROUND_HOOK_FLASH_PRESSURE_FREE_FLOOR`）。空闲块低于它时作业暂停写入，只靠回收（每步一次调用）把空闲块抬回来，回到下限以上再写；默认 0 不设下限。
- **回收由谁调**：#220 起由后台任务自己的回收作业调（100 ms 周期、一次一块，播放和暂停时也调，见上“回收作业”），钩子不再另调：`BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM` 默认 0，钩子构建里的擦除节拍和产品一致（回收作业每 100 ms 一次、一次一块）；钩子的写把空闲块压到水位以下之后，回收作业给的机会才擦除。这个常量原来是 1（#213）：当时 Storage Task 有打开的音频流时不回收，播放中就没有真擦除；#215 去掉了那个条件，钩子构建里两处都在回收，擦除节拍比产品高一倍（第 4 次上板的 R4.1 用那个构建，负载更重）。需要对照原来的更重负载时，configure 时把宏改回 1 重编。**R6.2 的“播放中真擦除、欠载为 0”现在量的就是产品节拍。**

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

回收作业和钩子是同一个后台任务里的两个作业，所以钩子的写（最长约 1.5 至 3 s）会让这一圈回收作业晚到（同一任务顺序执行），回收在钩子的写之后才调；这是钩子构建里的预期（第 4 次上板里 Storage 的回收等钩子的写最长 1.03 s，那时它们是两个任务，现在是同一个任务的两个步）。音频读取任务只碰 SD 卷、不碰 Flash 卷，前台读标签也不碰 Flash 卷，所以钩子构建里曲名不再像第 4 次上板那样晚 1 至 2 s 出来。**对 SD 卷与音频读取的影响应为零**——主机测试只能证明作业不调 SD 的接口，真实的“互不影响”靠第 4 次上板（R4.1、R4.2）。

### 消息池（DIAG-3；Service Log 当时 30 块，#218 加大到 45 块，Log Task 每 5 ms 转交一条）

**#220 重算**（保守口径，上限留至少 8 块，用户 2026-11-06 的决定）：删掉 Storage Task 之后永久任务 10 个（IDLE、Tmr Svc、Log、Audio Read、Volume、Playback、Monitor、GUI、Background、Foreground），Monitor 一批稳态 15 行，开机后 `MountFlash` 期间多一个 Flash 启动任务 16 行（`MONITOR_TASK_CONCURRENT_MAX` 12 → 11）。其他任务最坏同时到达 17 → 18 行：加后台任务的回收失败 1 行（失败一次就停，所以只有一条）；扫描的日志不另加：插卡或拔卡发生在扫描中时没有音频流（读取被打断、`Stop reason`、`CARD_REMOVED lat` 三行不会同时出现），位置由扫描的日志占用，而**一轮扫描最多 3 行日志**（#220 审阅后的上界）：节流后的第一条 WARN（目录打不开、目录栈满每轮只记一条，其余计数进结束行的 `skipped=`）或截断的 WARN、一条 ERROR（读或关目录失败，原来读失败是两条，合成一条）、`Scan done`；卷路径 5 行加这 3 行仍在原来“一次拔卡或插卡 8 行”之内。主机测试 `test_a_scan_logs_at_most_three_lines_even_when_everything_goes_wrong` 钉住这个上界。**默认构建最坏同时到达 16 + 18 = 34 行，留 11 块**（和 #218 的 34 行同数：少一个任务、多一条回收失败）；Flash 压力钩子构建再加统计行 1 + 卷满 1 + 停下 1 = 37 行，留 8 块；翻页注入钩子构建 35 行留 10 块。`monitor_task.c` 的 `_Static_assert` 核对 `MONITOR_TASK_CONCURRENT_MAX + 5 + MONITOR_POOL_OTHER_WORST_LINES + MONITOR_POOL_SPARE_MIN <= LOG_SERVICE_QUEUE_LENGTH`：11 + 5 + 18 + 8 = 42 ≤ 45。钩子的统计行节拍 10 s，不是每步一条；每行正文不超过 127 字符（主机测试在极端取值下核对）。扫描和回收没有周期性的日志行：扫描每轮一行 `Scan done`（有被节流的 WARN 时带 `skipped=`），回收只有失败那一条。**第 6 次上板追加失败诊断后行数不变**：回收失败行和钩子停下行都仍是一行，只是正文变长（不超过 127 字符，超出的尾巴被截断），所以上面的行数核算不动；诊断本身不另投日志（读 Platform 里已有的记录）。

### Flash 失败诊断（`background_flash_diag`，第 6 次上板追加）

**为什么**：文件接口把 FTL 的任何非 OK 结果（`NO_SPACE`、`CORRUPT`、`IO_ERROR`……）都压成 `SERVICE_ERROR`（数值 1），第 6 次上板压到底后钩子停在 `stopped at=write status=1`，原因看不出来。FTL 自己的“最近结果”每次请求都会被覆盖，失败后卷又不就绪，事后读不到，所以 Platform 在请求以非 OK 收尾的那一刻记下，之后的请求不冲掉它。**只读诊断，不改变任何产品行为。**

**数据来源**：`Platform_Flash_GetVolumeDiagnostics()`（既有诊断通路）的新字段 `LastFailure`（FTL 结果归一化后的分类）、`LastFailureOperation`（打开、格式化、读、写、同步、回收）、`LastFailureRawStatus`（失败时 FTL 最近一次原始访问的结果）、`LastFailureDeviceError`、`LastFailurePortStatus`（失败时芯片后端快照）、`FailureCount`（开机以来以非 OK 收尾的请求数）、`LastRejection`（最近一次被 FTL 在受理时就拒绝的请求，例如卷故障后的 `NOT_READY`）。新增的是 `Platform_Flash_VolumeDiagnosticsTypeDef` 末尾的字段和两个枚举，没有新增函数；Filesystem Service 的接口没有改。

**格式**（回收失败行和钩子停下行共用，`background_flash_diag.h`）：

```
… ftl=<结果> op=<请求> raw=<原始访问> n=<次数> free=<空闲块>[ valid=<有效> stale=<失效>][ <调用者的额外段>][ rej=<受理拒绝>][ hw=<芯片错误>/<端口状态>]
```

| 字段 | 含义 | 怎么读 |
|---|---|---|
| `ftl` | FTL 最近一次以非 OK 收尾的请求的结果：`NO_SPACE`（无空闲块且无可回收块）、`CORRUPT`（提交关联、数据或擦除回读验证不符）、`IO_ERROR`（原始访问失败或被中止）、`NOT_READY`、`INVALID`、`INCOMPAT`、`INCOMPL`、`UNFORMAT`、`OTHER`；`none` 是 FTL 从没失败过 | `none` 说明文件接口的错误不是 FTL 来的（去 FatFs 或 Filesystem Service 找）；`CORRUPT` 与 `IO_ERROR` 会让 FTL 进入故障（卷不再就绪），`NO_SPACE` 不会 |
| `op` | 失败的那次 FTL 请求：`open`、`format`、`read`、`write`、`sync`、`reclaim` | 和停下行的 `at=` 对照：`at=write` 而 `op=reclaim` 说明失败是先前一次回收留下的，当前请求只是撞上了已故障的卷 |
| `raw` | 失败时 FTL 最近一次原始访问：`ok`、`busy`、`error`、`timeout`、`?`（Platform 读不到） | `error`、`timeout` 指向 QSPI 或芯片；`ok` 说明原始访问都成功，是 FTL 自己的校验不过 |
| `n` | 开机以来以非 OK 收尾的请求数（最多写 999） | 1 是第一次失败；每次重启复位 |
| `free` | **失败后**的空闲块数 | 和统计行 `free=` 比较，看失败时是否已耗尽（FTL 下限约 2） |
| `valid`、`stale` | 失败后的有效、失效块数，**两者都为 0 时省略** | FTL 故障后不就绪，这两个读作 0，所以停下行另带写之前的 `pre=` |
| `rej` | 最近一次被 FTL 在受理时就拒绝的请求的结果，没有则省略 | 例如 `NOT_READY`：重启后第一个请求被已故障的卷拒绝（`at=open status=1` 就是这样） |
| `hw` | 失败时芯片后端错误码和端口状态（十六进制），都为 0 则省略 | 非 0 指向硬件 |
| `diag=n/a` | Platform 读不到（FTL 还没绑定） | 取代以上全部字段 |

不超过 127 字符：必有部分（`ftl`、`op`、`raw`、`n`、`free`）和调用者的额外段在前，`rej`、`hw` 在后，放不下先截断后面的。典型一行约 100 字符。

### 主机测试证明什么、不证明什么（HAR-5）

`Tests/background_task`（见该目录 README）：
- **循环**：每圈各一次、最小等待、通知提前醒、作业运行期间到的通知不丢、**卷事件落在通知槽的 VOLUME 位上、叫醒睡眠（含无限等待）并被一次取走**。
- **作业表**：内置作业（回收，钩子打开时再加压力钩子）在前，注册表各应用的 `background_step` 按表序在后，空字段不占位；默认构建里没有压力钩子的符号。
- **回收作业**（`background_reclaim_tests`）：卷序号为 0 不调用；第一次机会在一个周期之后；一次最多一块（落后很多个周期也只调一次）；被别的作业频繁叫醒时周期不变；卷没挂载不调用；失败（`SERVICE_ERROR`、`SERVICE_TIMEOUT`）一次就停并记一条 ERROR（含状态数值和 FTL 失败诊断，不超过 127 字符；`ftl=none`、`diag=n/a`、最坏取值各一例），停下后无论过多久都不再调用；卷不可用类和忙碌的返回值不算失败；卷序号变化后恢复，不再记旧的失败。
- **失败诊断片段**（`background_flash_diag_tests`）：每种分类、请求类别、原始结果的名字，没有失败过、写失败后的第 6 次上板现象、可选段（`valid/stale`、额外段、`rej`、`hw`）的出现与顺序、失败次数封顶 999、读不到诊断、缓冲从 1 字节到 128 字节逐个容量的截断安全（不越界、总以 NUL 结尾、是完整内容的前缀）。压力钩子的停下行另有 6 例（故障后、卷仍就绪、FTL 从没失败、非写步没有写字段、读不到诊断、最坏取值下写请求字节数与文件已写字节不被截断）。
- **压力钩子**：步序与返回值、卷没就绪时不调文件接口、各类失败的归类、新建 BUSY 的重试上限、空闲块下限暂停写入、日志格式与节拍、钩子自己的回收默认关闭（静态断言）。
- **优先级排序**（`test_task_priorities.c`，编译期）：播放 4 > 音频读取、卷 3 > 前台、GUI、Log、Monitor 2 > 后台、Flash 启动 1 > Boot 0；Boot 栈 192 word。

**不证明**：Platform 在 FTL 哪一步记下哪个结果（要上板用“压到底复现”版读出来）、真实任务切换与优先级继承、`xTaskNotifyWaitIndexed` 在真实内核里的睡眠与醒来延迟、与 GUI 同级时的 CPU 份额、**开机扫描时长（后台任务与 Flash 启动任务同级的真实轮转）**、真实的 Flash 持锁时长与擦除、播放中真擦除欠载是否为 0、SD 与 Flash 的并发是否互不影响、任务栈余量（R6.8，Monitor 栈行）、`background_task.c` 的静态创建与订阅。这些靠第 6 次上板（R6.1 至 R6.10 与开机扫描时长）。

## 给后面票的注意事项

- **#221、#222**：Storage Task 和 `APP/tasks/storage/` 已不存在；ROUTES 里 `APP/tasks/storage/` 的条目要换成各任务（inbox 的 `route` 行）。播放状态机里存储窗口 Ops 仍叫 `Storage`（#215 用户决定 #222 收尾时只改名）。
- **第 6 次上板之后**：R6.2 起 STOR-12 的“播放中真擦除、欠载 0”才有证据（回收作业按产品节拍），通过后 STOR-12 才能升回 settled。
- **扩作业**：新增后台作业只在注册表多一行 `background_step`；若要临时提高后台任务优先级（封面解码，#196），只改后台任务内部，单步函数的接口不变。
