# 播放任务

播放任务持有唯一的播放状态机：它是播放游标的唯一写者，向音频读取任务写“想要的流”，把它交来的音频块交给 `Service_Playback_*` 解码与输出，并发布给 GUI 的播放快照。所有权按 [ADR-0020](../../../docs/adr/0020-playback-state-machine-ownership.md)，来源是 [#71](https://github.com/immorcoding/mp3_project/issues/71) 的 K1–K5、B1–B17。

本目录包含**公开合同**、**纯 C 状态机**（[#78](https://github.com/immorcoding/mp3_project/issues/78)、[#79](https://github.com/immorcoding/mp3_project/issues/79)）和**任务外壳**（`playback_task.c/.h`，[#80](https://github.com/immorcoding/mp3_project/issues/80)）。外壳把命令 FIFO、状态信箱、音频读取任务的音频流、`Service_Playback_*` 与 Platform 音频事件接到状态机上；状态机负责命令（含跳转 SEEK）、流交接、切歌、暂停、播完，以及坏文件跳过、T_data/T_out 超时、播放模块与 Platform 故障、AUDIO_FAULT 锁存和 B6 停止原因表。

## 公开 Interface

- `playback_contract.h`：音乐应用 GUI 分区（`APP/apps/music/gui/`）与模拟器脚本化后端共用的唯一合同，只含类型（`Playback_CommandTypeDef`、`Playback_SnapshotTypeDef` 等）与 `playback_command_send()`、`playback_snapshot_read()` 两个声明，字段以 #71 K1 为基础，0.9.0 加入跳转（[#172](https://github.com/immorcoding/mp3_project/issues/172) C 节、[#176](https://github.com/immorcoding/mp3_project/issues/176)）：命令种类末尾新增 `PLAYBACK_COMMAND_SEEK`，`Generation`/`Index` 由“仅 SELECT”改为“SELECT 与 SEEK”的曲目身份，命令新增 `TargetMs`（毫秒，仅 SEEK），快照新增 `TrackStartCount`，不暴露 FreeRTOS 句柄。GUI 里只有音乐分区（`APP/tasks/gui/music`）可以包含它，`Service/gui` 不得包含。真实后端与模拟器的假后端实现同一个头，在链接时互换。
- `playback_task.h`：任务入口 `playback_task()`、私有通知槽 `Playback_NotifyIndexTypeDef`（`PLAYBACK_NOTIFY_EVENT`）与只读的 `playback_task_get_underrun_total()`（开机以来欠载累计，Monitor 读）；由 `app_tasks.c` 以 `xTaskCreateStatic` 创建，不是跨模块 Interface。`playback_command_send()` 与 `playback_snapshot_read()` 的实现在 `playback_task.c`。
- `playback_task_config.h`：事件 FLAG、命令 FIFO 深度与等待节拍等固定配置。
- `playback_machine.h`：状态机的 Ops 与两个入口，只服务播放任务外壳，不是跨模块 Interface。
  - `playback_machine_init(machine, ops)`：快照全零（停止、无当前曲），不发布。
  - `playback_machine_underrun_total(machine)`：开机以来欠载累计（DIAG-6），只读。
  - `playback_machine_step(machine, commands, command_count, now_ms)`：一次唤醒的全部工作。命令由外壳从命令 FIFO 取空（≤ 8 条）后传入；`now_ms` 是外壳传入的单调毫秒时刻（B9），状态机不读 tick、不依赖 FreeRTOS。

## 任务外壳

`playback_task()` 由 `app_tasks.c` 静态创建（优先级 `APP_PLAYBACK_TASK_PRIORITY` 4（#211 起由 3 升到 4，高于卷任务 3；播放不碰文件、不拿任何文件锁，所以不受卷锁上的优先级反转影响）、栈 `APP_PLAYBACK_TASK_STACK_WORDS` 5120 word = 20480 B，TCB 与栈在 DTCM `.bss`），晚于 Audio Read Task 创建；请求信箱、流记录信箱与块池由 Boot 任务在创建它之前初始化（`audio_reader_boot_init()`，#215），所以它一启动就可以写请求信箱。

- **启动**：创建命令 FIFO 与状态信箱 → `Service_Playback_Init()` → `playback_machine_init()` → 发布任务句柄并注册 `audio_reader_stream_set_wake_callback()` → `Platform_Audio_SetEventCallback()`。任一步失败则记日志并删除本任务，快照保持全零，`playback_command_send()` 返回 `PLAYBACK_QUEUE_FULL`。
- **事件槽** `PLAYBACK_NOTIFY_EVENT` 的 FLAG：`PLAYBACK_NOTIFY_FLAG_COMMAND`（`playback_command_send()` 入队后置位）、`PLAYBACK_NOTIFY_FLAG_STREAM_RECORD` 与 `PLAYBACK_NOTIFY_FLAG_BLOCK_READY`（音频读取任务的唤醒回调在音频读取任务上下文置位）、`PLAYBACK_NOTIFY_FLAG_AUDIO`（Platform 音频事件回调在 ISR 中经 `xTaskNotifyIndexedFromISR` 置位，ARC-10）。FLAG 只用于唤醒，醒来后以信箱、队列与 `Platform_Audio_GetStatus()` 计数为准，所以通知被合并或迟到都不丢事实。
- **每次唤醒**：有界等待事件槽（无事件时最长 `PLAYBACK_TASK_WAKE_PERIOD_MS` = 100 ms，让 T_data、T_out 这类只靠时刻到期的判定在没有 FLAG 时也能评估）→ 把命令 FIFO 取空（≤ 深度 8 条，不按唤醒次数计数）→ 以 `xTaskGetTickCount()` 换算的单调毫秒时刻（B9，要求 tick 为 1 kHz）调用 `playback_machine_step()`。
- **命令 FIFO**（K2）：深度 `PLAYBACK_COMMAND_QUEUE_DEPTH` = 8 的静态 FreeRTOS 队列；`playback_command_send()` 零超时入队，满、参数为空或任务尚未启动返回 `PLAYBACK_QUEUE_FULL`，成功后置命令 FLAG。任务上下文调用，不得在 ISR 调用。
- **状态信箱**（K2、B17）：长度 1 的静态覆盖队列；状态机每次快照变化经 `PublishSnapshot` 整份 `xQueueOverwrite`，`playback_snapshot_read()` 用 `xQueuePeek` 窥视，不取走；首次发布前读到全零快照。不唤醒 GUI。
- **音频读取任务映射**（0.10.0 前叫 Storage 映射，#215 改名，字段、枚举和语义不变）：请求信箱含 `AudioOffset`（切歌与普通开流为 0），外壳逐字段映射并由 `_Static_assert` 固定偏移。状态机自带 `Playback_Machine{Request,Record,Block}TypeDef` 与结果、终止枚举（因为 `audio_reader_stream.h` 引入 FreeRTOS，状态机不能包含它，#78），外壳逐字段映射到 `audio_reader_stream_request()`、`audio_reader_stream_get_record()`、`audio_reader_block_peek_ready()/take_ready()` 与 `audio_reader_stream_return_block()`，并用 `_Static_assert` 固定两边结构体大小、字段宽度与偏移、枚举值的一一对应；任何一边改字段或枚举，编译期报错。不改音频读取任务的公开头。#215 当时播放状态机本身没有改；它内部指向音频读取任务的 Ops 原叫 `Storage`，#222 收尾时只改名为 `Stream`（`Playback_MachineStreamOpsTypeDef`、`Ops.Stream.*`，与 `audio_reader_stream_*` 同名），行为不变。
- **播放模块映射**：`Playback_MachinePlayerOpsTypeDef` 直接填 `Service_Playback_*` 函数指针（含 `MapSeek` 与 `ReopenStream`）。
- **B2**：外壳不碰 FatFs/LVGL，不同步等待音频读取任务（只写请求信箱、读流记录信箱、取放块队列，全部零超时），不阻塞发送，没有运行期 malloc。命令 FIFO 与状态信箱是静态队列，在 DTCM `.bss`，不占内核堆（#71 F 节原计两个堆对象，实现改为静态，堆余量更大）。
- **不做主机测试**：外壳依赖 FreeRTOS、音频读取任务与 Platform，进板级验收；状态机逻辑由 `Tests/playback_task/` 覆盖。

## 状态机的一次唤醒

顺序按 B1：读流记录（只认当前流序号，旧序号的丢弃，B3）→ 按开启结果与终止事实反应（B6）→ 归还手中的旧序号块与就绪队列队首连续的旧序号块（≤ 32，B15）→ 逐条处理命令（K3，序号回显含被忽略的命令）→ 把手中块的剩余字节交给播放模块 → 推进输出管线（`Service_Playback_Pump()`），输入不够就继续交字节，没有进展即结束本次唤醒去等块；最后判定 T_out 与 T_data 是否到期。

- **命令表**：TOGGLE 按自身状态决定方向；SEEK 见下节“跳转”；NEXT/PREV 在播放中环形步进并切歌、在暂停中换曲保持暂停、在停止态只移动游标，无游标（或曲目数未知）时忽略；SELECT 在三态下都切到该曲播放，选中当前曲也换新流序号。
- **游标**：就是快照里的 `{HasTrack, Generation, Index}`，加上最近一次开启 OK 带回的曲目数 N。停止态无游标时按播放打开 `{代次 0, 位置 0}`；开启 OK 把代次换成应答代次并更新 N。NEXT/PREV 用游标的代次；SELECT 用命令携带的代次，代次未变时沿用已知 N，代次变化则 N 未知直到下一个开启 OK。N 未知时 NEXT/PREV 被忽略，因为无法环形步进。
- **切歌清理顺序**（B11）：游标前进，分配新流序号并写请求信箱；播放模块放弃旧流（Platform Stop）；归还手中旧块；队列里的旧序号块取出即归还；新流 OK 后重新开流、首帧、预填、Start。自动前进走同一路径。
- **暂停**：暂停 = 播放模块暂停输出，流保持。暂停中换曲在开启 OK 后 `BeginStream` 再立即暂停输出，使推进只解首帧并预填，不出声（B12）；开启应答到达之前就被暂停的流同样处理。
- **播完**（B13、B14）：流记录 ENDED 且手中无块、队列无本流的块时告知播放模块输入读尽（只一次）；播放模块回报流尾放完后游标前进（末曲回首曲），不出现停止态。短文件按同一条流记录依次处理。
- **块的持有**（B15）：同一时刻最多持一块，字节全部拷进窗口后立即归还；块比窗口大时手中保持，余量下一轮继续交。
- **快照发布**（B17）：每解出一帧更新 `PositionMs`，其他字段一变化即整份发布；没有变化不发布。总时长在接受切歌命令的同一刻变为未知，首帧给出后才填。位置在切歌或重新开播时回 0，跳转时设为落点（可变小），其余时候只增不减。
- **`TrackStartCount`**：开机以来从曲首开始播放一首曲目的次数，单调。每次经切歌路径开新流时加 1：SELECT（含同曲重选）、NEXT/PREV（播放与暂停态）、停止态 TOGGLE 开播、播完前进、跳过坏文件后开下一首（连跳时每开一首加 1）；停止态下的 NEXT/PREV 只移动游标、不开流，所以不加。跳转（含 `TargetMs = 0`）不加。GUI 据此判断唱盘是否归零，不再看位置变小。

## 跳转（SEEK，[#176](https://github.com/immorcoding/mp3_project/issues/176)）

决定来源：[#166](https://github.com/immorcoding/mp3_project/issues/166)、[#164](https://github.com/immorcoding/mp3_project/issues/164)、[ADR-0021](../../../docs/adr/0021-seek-by-reopen-with-audio-offset.md)、DIAG-5。跳转 = 带音频区间偏移重开一条新流，不在原文件内 seek；毫秒到字节的映射在 `Service_Playback_MapSeek()`（见 [Service/playback](../../../Service/playback/README.md)），状态机不认识 MP3，也不硬编码钳位，只用映射返回的 `{AudioOffset, LandingMs, Map, Clamped}`。

- **静默忽略**：无当前曲或处于 STOPPED；`(Generation, Index)` 与当前曲不符；快照总时长为 0（切歌后首帧之前、开流等待中）；映射失败（播放模块没有流级事实）。只记一条 DEBUG（`LogDebug`），不改三态、不计停止、不写请求，**照常推进 `HandledCommandSeq`**。越尾不算忽略。
- **最新者胜**：同一次唤醒取出的命令里，连续的多条 SEEK 只执行最后一条，被合并的不执行、不记日志，但序号照常推进；中间隔着别的命令（如 SEEK、TOGGLE、SEEK）不合并。跨唤醒的新跳转覆盖旧跳转：开新 StreamSeq，旧流的应答与块按流序号被丢弃，旧跳转的 Seek INFO 作废。
- **执行**（同一步，顺序按切歌的 B11）：① 以新流序号与 `AudioOffset` 写请求信箱；② 放弃播放模块的旧流（Stop 失败按 AUDIO_FAULT 停止，与切歌一致）；③ 归还手中旧块，就绪队列里的旧块取出即归还；④ **同一步**把 `PositionMs` 设为落点（允许变小），随后由命令处理推进 `HandledCommandSeq`，所以 GUI 看到回显时快照已是落点。不改三态、不计 `StopCount`、不清 `DurationMs`、不加 `TrackStartCount`。
- **开启 OK 之后**：用 `Service_Playback_ReopenStream(LandingMs)` 代替 `BeginStream`，保留总时长与采样率，所以不再有 `FirstFrame`（也就没有第二条 `Open` INFO）；暂停中紧接着 `PauseOutput`，走 B12 预填，不出声。重开失败按 AUDIO_FAULT 停止（与 `BeginStream` 被拒一致）。
- **拒收旧流的位置**（`SeekAwaitReopen`）：从执行跳转到新流重开之间，快照不接受任何 `DecodedMs` 回报，否则位置会被旧值顶回去。此间 `StreamBegun` 为假，状态机根本不调用 `Pump`，这个标志是对同一约束的显式防线。重开之后 `DecodedMs` 从落点起算（不小于落点），位置照旧只取较大值。
- **作废**：切歌类命令（NEXT/PREV/SELECT/停止态 TOGGLE）、播完前进、跳过坏文件、进入停止态都使未完成的跳转作废，不再重开、不记 Seek INFO。
- **T_data 与 T_out 与切歌一致**：新流开启应答从写请求起算 T_data；放弃旧流时停 T_out，下一次 `OutputStarted` 重新计时；不新增计时器，不改超时值。
- **越尾**：映射把偏移钳在 `AudioBytes − W_min`，之后照常读完、播完、前进；暂停中则停在末尾，恢复后放完再前进（`Pump` 在暂停态不回报流尾放完）。钳位值的大小由 Service 决定，状态机不依赖它。高码率文件（如 320 kbps）钳位后可能一帧也解不出（见 Service/playback README 的 #166 第 11 条核实），播放模块此时没有可放的音频，暂停中也会回报流尾放完。状态机用 `SeekHoldEnd`（跳转重开的流、尚未起播过）处理：暂停中收到流尾放完不前进，保持 PAUSED、位置为落点（此时记 Seek INFO，因为没有预填可等）；按 TOGGLE 恢复后同一事实再次到来，按播完前进，`TrackStartCount` 照常加 1。PLAYING 下零帧直接按播完前进，前进之前补记同一格式的 Seek INFO（含 `clamp` 与 `lat`，与暂停中零帧一致；前进会作废跳转，不补记就丢这一行）。`OutputStarted`、切歌与停止清除该标志，所以此后的暂停在流尾行为不受影响。
- **落点之后丢帧**：重开后位储备不足的帧被丢弃（实测最多约 4 帧，低码率 VBR 约 104 ms），状态机仍以映射落点为快照位置，不另行修正。
- **Seek INFO**（DIAG-5，播放任务记，不含文件名）：`Seek idx=N tgt=<ms> land=<ms> off=<B> map=toc|lin[ clamp] lat=<us>`。PLAYING 在输出重新起播（`OutputStarted`）时记；PAUSED 在预填完成时记（补半区不再要输入，且流未读尽到无音频可放）；两者先到者先记，只记一次。`lat` 是处理 SEEK 命令到记日志的周期差，用 `ReadCycles`（DWT）与 `CyclesToUs` 换算，外壳接 `CortexM7CycleCounter_*`；Ops 为空时记 0。忽略记 DEBUG，被合并与作废的不记。时延初值：PLAYING 下 `lat` ≤ 100 ms，PAUSED 下只记录，超出由板测修正。

## 停止原因、坏文件与故障（#79）

所有停止出口都经 `enter_stopped()`：先写“无流”请求，再让播放模块放弃流（Platform Stop），归还手中块，记录原因、位置与总时长清零、停止次数 +1、清零连跳计数。游标保留还是重置按 K4：拔卡、NOT_READY、STALE、NO_TRACK 重置为无当前曲（曲目数 N 作废），其余保留。

| 事实 | 来源 | 反应 | 游标 | 计入连跳 |
|---|---|---|---|---|
| 开启 BAD_FILE | 流记录 | 跳过并提示 | 按方向前进 | 是 |
| 开启 IO_ERROR | 流记录 | 停止 READ_ERROR | 保留 | 否 |
| 开启 NOT_READY / STALE / NO_TRACK | 流记录 | 停止 NOT_READY / CATALOG_CHANGED / NO_TRACK | 重置 | 否 |
| 终止 READ_ERROR | 流记录 | 停止 READ_ERROR | 保留 | 否 |
| 终止 CARD_REMOVED | 流记录 | 立即停止 CARD_REMOVED | 重置 | 否 |
| 同步预算 R 耗尽、Start 因参数被拒 | `Pump` 回报 | 跳过并提示 | 按方向前进 | 是 |
| TRANSFER_ERROR、`Pump` 返回 `SERVICE_ERROR`、T_out 到期 | `Pump` / 计时 | Stop → 停止 AUDIO_ERROR（可重试） | 保留 | 否 |
| STOP_FAILED、`BeginStream`/`PauseOutput`/`AbandonStream` 失败 | `Pump` / 返回值 | 停止 AUDIO_FAULT 并锁存 | 保留 | 否 |
| T_data 到期 | 计时 | 写“无流” → Stop → 停止 NO_DATA | 保留 | 否 |

- **坏文件跳过**（B5）：`SkippedBadCount` 每次 +1；沿最近一次移动的方向前进（PREV 向前，NEXT、SELECT、停止态 TOGGLE、播完前进向后），暂停态保持暂停。连跳计数达到最近一次开启 OK 带回的 N 即停止 ALL_BAD（游标保留，跳过数增加 N）。计数在输出真正 Start（`OutputStarted`）时清零，不在首帧解出或开启 OK 时清零；用户换曲命令与进入停止态也清零。不在首帧清零，是因为 Start 被拒可能出现在比首帧更晚的一次 `Pump` 里，否则整库采样率都被拒的曲目会无界轮转到不了 ALL_BAD（B5 与 B6 合起来的缺口）。N 未知（开机后第一首就是坏文件，且应答没有带回曲目数）时无法计数，按 NO_TRACK 停止；BAD_FILE 应答若带回代次与曲目数（音频读取任务位置通过校验后会填，其余失败为 0），状态机采用它们。
- **T_data**（B7，`PLAYBACK_MACHINE_DATA_TIMEOUT_MS`，**初值 5000**，#215 由 2000 改，#202 M 节授权；依据改成“≥ 2 × 最长非故障等待”：最长非故障等待是 12000 首同文件夹、定位符失效时按路径打开最后一首的 2.05 s，无论是音频读取任务自己回退，还是在等前台回退放 SD 锁；两边同时回退约 4.1 s，5 s 也盖得住。真正的读卡故障约 500 ms 就报 `READ_ERROR`，T_data 只负责抓读取任务真的卡死这类异常，所以显示 “Read timeout” 要等 5 s。不写编译期断言，由持锁统计在板级验收核对（R4.2）。原来的依据“≥ 2 × Storage 单圈最大耗时”随主循环的消失而作废）：只在“需要数据却没有”时计时——等开启应答（从写请求起算，暂停中也算）；或本流未终止、播放模块补半区时回报输入不够、手中无块且队列没有本流的块（B16，预填、断流、欠载后的补满）。收到本流的应答或块重新计时；暂停且输出已满、排空（ENDED 之后）、停止时播放模块不要输入，自然不计时。
- **T_out**（B8，`PLAYBACK_MACHINE_OUTPUT_TIMEOUT_MS`，初值 500）：`OutputStarted` 起算，每次 `OutputProgress` 重新计时；暂停、放弃流、流尾放完、欠载（播放模块已 Stop）时停止计时，直到下一次 `OutputStarted`；恢复输出成功即视为 Start 并从恢复时刻起算。到期与 TRANSFER_ERROR 同路。判定放在本次推进之后，迟到的唤醒不会把已经发生的半区释放误判为无进展。到期都是“经过时间达到阈值”（≥），时刻按差值比较，回绕安全。
- **AUDIO_FAULT 锁存**（B10、K3）：之后停止态的 TOGGLE、SELECT 不开流、不动游标，只让停止次数 +1；NEXT/PREV 只移动游标。切歌途中 Stop 失败时，已写的新流请求被“无流”覆盖，游标停在已前进的位置。因其他原因停止时 Stop 失败，原因升级为 AUDIO_FAULT（停止次数仍只 +1）。锁存期间没有流，所以拔卡、代次变化没有可处理的流事实；重启才解除，不自动复位 MCU。
- **R**：同步预算是播放模块的配置常量（`PLAYBACK_SERVICE_SYNC_BUDGET_BYTES`），T_data、T_out 在 `playback_task_config.h`。

## 与外部的接缝

状态机不包含音频读取任务或 FreeRTOS 的头，外部依赖都经 `Playback_MachineOpsTypeDef` 注入，由 `playback_task.c` 装配：

- 音频读取任务：写请求信箱、窥视流记录、窥视/取走就绪块、归还块。类型与 `audio_reader_stream.h` 的请求、记录、结果枚举一一对应，外壳做 1:1 映射并以静态断言固定。
- 播放模块：`BeginStream`、`Feed`、`FinishInput`、`Pump`、`PauseOutput`、`ResumeOutput`、`AbandonStream`，签名即 `Service_Playback_*`，外壳直接填函数指针。
- 状态信箱：`PublishSnapshot` 覆盖写。
- INFO / DEBUG 日志：`LogInfo(text)`、`LogDebug(text)` 可为空；外壳接到 `Service_Log_Post(INFO / DEBUG, "PLAYBACK", text)`（非阻塞，队列满丢弃）。单条 INFO 文本缓冲 `PLAYBACK_MACHINE_LOG_TEXT_SIZE` 为 96 B（Seek 行最长约 88 B，小于 Service_Log 单块正文 128 B）。
- 周期计数器：`ReadCycles()`、`CyclesToUs(cycles)` 可为空；外壳接到 `CortexM7CycleCounter_Read()` 与按核心频率的换算，只供 Seek INFO 与 Switch INFO 的 `lat`。
- 播放模块的跳转入口：`MapSeek`、`ReopenStream`，外壳直接填 `Service_Playback_MapSeek` 与 `Service_Playback_ReopenStream`。

## INFO 日志事件

状态机只在命令与曲目边界记 INFO（被静默忽略的 SEEK 只记 DEBUG），频率随命令与曲目数有界；`Platform/audio`（ISR 回调路径）与 `Service/playback`（每帧路径）不记 INFO。只记曲库序号 `idx`，不记文件名（曲名属于曲名数据路径）。文案固定，主机测试 `playback_task_log` 按顺序断言：

| 文案 | 时机 |
|---|---|
| `Start idx=N` | 停止态 TOGGLE 开播，或 SELECT 选中并播放 |
| `Open idx=N rate=R` | 首帧解出（开流成功，R 为采样率 Hz） |
| `Pause` / `Resume` | 播放中 TOGGLE 暂停 / 暂停中 TOGGLE 恢复 |
| `Next idx=N` / `Prev idx=N` | 用户切歌，N 为新序号 |
| `Skip bad idx=N` | 跳过坏文件（含 Start 被拒、同步预算耗尽），N 为被跳过的曲目 |
| `Finished idx=N` / `Auto idx=M` | 播完，随后自动前进到 M |
| `Seek idx=N tgt=T land=L off=B map=toc\|lin[ clamp] lat=U` | 跳转执行成功后：PLAYING 在输出重新起播时，PAUSED 在预填完成时（见“跳转”） |
| `Switch idx=N lat=U` / `Switch idx=N lat=U auto` | 用户切歌（NEXT、PREV、SELECT）或自动前进（播完前进、跳过坏文件续上）后输出重新起播时（DIAG-6，见下） |
| `Stop reason=NAME` | 进入停止态，NAME 为快照 `StopReason` 的枚举名（去掉 `PLAYBACK_STOP_REASON_` 前缀） |

一次「播放 → 切歌 → 暂停 → 停止」因此可按日志顺序重建，停止原因与快照一致。

**Switch INFO**（DIAG-6、#204，常开，每次换曲一行，曲目边界）：用户切歌记 `Switch idx=N lat=U`，自动前进记 `Switch idx=N lat=U auto`；`N` 是换到的曲库序号，`U` 是微秒。`lat` 在固件里算好：状态机处理这条切歌命令（或决定自动前进）时读一次周期计数器（`ReadCycles`），输出重新起播（`OutputStarted`）时再读，差值经 `CyclesToUs` 换算；用户切歌的 `lat` 是“命令被播放任务处理到起播”，**不含**按键到命令入队的 GUI 一段，也不含命令在 FIFO 里等待的时间；自动前进的 `lat` 从状态机决定前进（流尾放完、游标前进）算到下一首起播，即曲间间隙，长跑（清单 1 C1、R7.1）靠它取边界证据。只在 PLAYING 下计：用户切歌要真正换了一首（`TrackStartCount` 加 1），自动前进包括播完前进和跳过坏文件后续上（不含 ALL_BAD 等停止）；暂停中换曲或前进不出声、不记。起播前被停止作废；起播前又来一次用户切歌则以最后一次为准、从最后一条命令算起，且不带 `auto`；自动前进或用户切歌之后遇到坏文件续上，沿用原起点与原种类，仍只记一行。停止态 TOGGLE 开播与跳转（有自己的 `Seek` 行）不记 Switch；开播后第一首就是坏文件而续上时，会记一行 `auto`（与长跑里的自动前进无法区分，R1.4 和 R4.1 只看不带 `auto` 的行）。Ops 缺 `ReadCycles` 或 `CyclesToUs` 时 `lat=0`。R1.4 的阈值：无负载 ≤ 50 ms（spike 参考约 22–26 ms）。

**欠载累计**（DIAG-6）：Platform 的欠载计数每次 Start 清零，状态机把播放模块每次回报的次数累加成开机以来的总数（`playback_machine_underrun_total()`），外壳经 `playback_task_get_underrun_total()` 公开只读。写者只有播放任务，32 位对齐读取不撕裂；Monitor 每 5 s 读一次，输出在 `Health` 行（见 [Monitor](../monitor/README.md)）。

## 编译期依赖

- 状态机（`playback_machine.*`）：`playback_contract.h`、`playback_task_config.h`、`Service/playback/playback_service.h`（只取 `Service_Playback_PumpReportTypeDef` 与 `Service_StatusTypeDef`）。不依赖 FreeRTOS、HAL、`APP/tasks/audio_reader/`。
- 任务外壳（`playback_task.c`）：状态机、`APP/tasks/audio_reader/` 的 `audio_reader_stream.h` 与 `audio_reader_block.h`、`Service/playback/`、`Service/log/`、`Platform/audio/` 的公开 Interface，以及 FreeRTOS 任务、队列与通知 Interface。不依赖 HAL、FatFs、LVGL。

## 主机测试

`Tests/playback_task/`：假音频读取任务与假播放模块（含线性假 `MapSeek`/`ReopenStream` 与假周期计数器），覆盖跳转（`playback_task_seek`：执行与落点、三种忽略、最新者胜、带偏移重开、暂停中预填、越尾、`TrackStartCount`、作废、计时、Seek INFO）、切歌时延与欠载累计（`playback_task_switch`：Switch INFO 的格式、触发、auto 与作废）、K3 全部格子、K5 的快照字段、流身份与块交接、切歌清理顺序、暂停、EOF 与短文件、游标环形步进与代次绑定，以及 B5 坏文件有界跳过、B6 每一行停止原因、B7/B8 超时、B10 与 K3 的 AUDIO_FAULT 锁存。
