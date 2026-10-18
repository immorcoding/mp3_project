# 播放任务

播放任务持有唯一的播放状态机：它是播放游标的唯一写者，向 Storage 写“想要的流”，把 Storage 交来的音频块交给 `Service_Playback_*` 解码与输出，并发布给 GUI 的播放快照。所有权按 [ADR-0020](../../../docs/adr/0020-playback-state-machine-ownership.md)，来源是 [#71](https://github.com/immorcoding/mp3_project/issues/71) 的 K1–K5、B1–B17。

本目录包含**公开合同**、**纯 C 状态机**（[#78](https://github.com/immorcoding/mp3_project/issues/78)、[#79](https://github.com/immorcoding/mp3_project/issues/79)）和**任务外壳**（`playback_task.c/.h`，[#80](https://github.com/immorcoding/mp3_project/issues/80)）。外壳把命令 FIFO、状态信箱、Storage 音频流、`Service_Playback_*` 与 Platform 音频事件接到状态机上；状态机负责命令、流交接、切歌、暂停、播完，以及坏文件跳过、T_data/T_out 超时、播放模块与 Platform 故障、AUDIO_FAULT 锁存和 B6 停止原因表。

## 公开 Interface

- `playback_contract.h`：GUI 音乐分区与模拟器脚本化后端共用的唯一合同，只含类型（`Playback_CommandTypeDef`、`Playback_SnapshotTypeDef` 等）与 `playback_command_send()`、`playback_snapshot_read()` 两个声明，字段与 #71 K1 逐字一致，不暴露 FreeRTOS 句柄。GUI 里只有音乐分区（`APP/tasks/gui/music`）可以包含它，`Service/gui` 不得包含。真实后端与模拟器的假后端实现同一个头，在链接时互换。
- `playback_task.h`：任务入口 `playback_task()` 与私有通知槽 `Playback_NotifyIndexTypeDef`（`PLAYBACK_NOTIFY_EVENT`）；由 `app_tasks.c` 以 `xTaskCreateStatic` 创建，不是跨模块 Interface。`playback_command_send()` 与 `playback_snapshot_read()` 的实现在 `playback_task.c`。
- `playback_task_config.h`：事件 FLAG、命令 FIFO 深度与等待节拍等固定配置。
- `playback_machine.h`：状态机的 Ops 与两个入口，只服务播放任务外壳，不是跨模块 Interface。
  - `playback_machine_init(machine, ops)`：快照全零（停止、无当前曲），不发布。
  - `playback_machine_step(machine, commands, command_count, now_ms)`：一次唤醒的全部工作。命令由外壳从命令 FIFO 取空（≤ 8 条）后传入；`now_ms` 是外壳传入的单调毫秒时刻（B9），状态机不读 tick、不依赖 FreeRTOS。

## 任务外壳

`playback_task()` 由 `app_tasks.c` 静态创建（优先级 `APP_PLAYBACK_TASK_PRIORITY` 3、栈 `APP_PLAYBACK_TASK_STACK_WORDS` 5120 word = 20480 B，TCB 与栈在 DTCM `.bss`），晚于 Storage Task 创建，以保证 Storage 已完成 `storage_audio_stream_init()`。

- **启动**：创建命令 FIFO 与状态信箱 → `Service_Playback_Init()` → `playback_machine_init()` → 发布任务句柄并注册 `storage_audio_stream_set_wake_callback()` → `Platform_Audio_SetEventCallback()`。任一步失败则记日志并删除本任务，快照保持全零，`playback_command_send()` 返回 `PLAYBACK_QUEUE_FULL`。
- **事件槽** `PLAYBACK_NOTIFY_EVENT` 的 FLAG：`PLAYBACK_NOTIFY_FLAG_COMMAND`（`playback_command_send()` 入队后置位）、`PLAYBACK_NOTIFY_FLAG_STREAM_RECORD` 与 `PLAYBACK_NOTIFY_FLAG_BLOCK_READY`（Storage 唤醒回调在 Storage Task 上下文置位）、`PLAYBACK_NOTIFY_FLAG_AUDIO`（Platform 音频事件回调在 ISR 中经 `xTaskNotifyIndexedFromISR` 置位，ARC-10）。FLAG 只用于唤醒，醒来后以信箱、队列与 `Platform_Audio_GetStatus()` 计数为准，所以通知被合并或迟到都不丢事实。
- **每次唤醒**：有界等待事件槽（无事件时最长 `PLAYBACK_TASK_WAKE_PERIOD_MS` = 100 ms，让 T_data、T_out 这类只靠时刻到期的判定在没有 FLAG 时也能评估）→ 把命令 FIFO 取空（≤ 深度 8 条，不按唤醒次数计数）→ 以 `xTaskGetTickCount()` 换算的单调毫秒时刻（B9，要求 tick 为 1 kHz）调用 `playback_machine_step()`。
- **命令 FIFO**（K2）：深度 `PLAYBACK_COMMAND_QUEUE_DEPTH` = 8 的静态 FreeRTOS 队列；`playback_command_send()` 零超时入队，满、参数为空或任务尚未启动返回 `PLAYBACK_QUEUE_FULL`，成功后置命令 FLAG。任务上下文调用，不得在 ISR 调用。
- **状态信箱**（K2、B17）：长度 1 的静态覆盖队列；状态机每次快照变化经 `PublishSnapshot` 整份 `xQueueOverwrite`，`playback_snapshot_read()` 用 `xQueuePeek` 窥视，不取走；首次发布前读到全零快照。不唤醒 GUI。
- **Storage 映射**：状态机自带 `Playback_Machine{Request,Record,Block}TypeDef` 与结果、终止枚举（因为 `storage_audio_stream.h` 引入 FreeRTOS，状态机不能包含它，#78），外壳逐字段映射到 `storage_audio_stream_request()`、`storage_audio_stream_get_record()`、`storage_audio_block_peek_ready()/take_ready()` 与 `storage_audio_stream_return_block()`，并用 `_Static_assert` 固定两边结构体大小、字段宽度与偏移、枚举值的一一对应；任何一边改字段或枚举，编译期报错。不改 Storage 的公开头。
- **播放模块映射**：`Playback_MachinePlayerOpsTypeDef` 直接填 `Service_Playback_*` 函数指针。
- **B2**：外壳不碰 FatFs/LVGL，不同步等待 Storage（只写请求信箱、读流记录信箱、取放块队列，全部零超时），不阻塞发送，没有运行期 malloc。命令 FIFO 与状态信箱是静态队列，在 DTCM `.bss`，不占内核堆（#71 F 节原计两个堆对象，实现改为静态，堆余量更大）。
- **不做主机测试**：外壳依赖 FreeRTOS、Storage 与 Platform，进板级验收；状态机逻辑由 `Tests/playback_task/` 覆盖。

## 状态机的一次唤醒

顺序按 B1：读流记录（只认当前流序号，旧序号的丢弃，B3）→ 按开启结果与终止事实反应（B6）→ 归还手中的旧序号块与就绪队列队首连续的旧序号块（≤ 32，B15）→ 逐条处理命令（K3，序号回显含被忽略的命令）→ 把手中块的剩余字节交给播放模块 → 推进输出管线（`Service_Playback_Pump()`），输入不够就继续交字节，没有进展即结束本次唤醒去等块；最后判定 T_out 与 T_data 是否到期。

- **命令表**：TOGGLE 按自身状态决定方向；NEXT/PREV 在播放中环形步进并切歌、在暂停中换曲保持暂停、在停止态只移动游标，无游标（或曲目数未知）时忽略；SELECT 在三态下都切到该曲播放，选中当前曲也换新流序号。
- **游标**：就是快照里的 `{HasTrack, Generation, Index}`，加上最近一次开启 OK 带回的曲目数 N。停止态无游标时按播放打开 `{代次 0, 位置 0}`；开启 OK 把代次换成应答代次并更新 N。NEXT/PREV 用游标的代次；SELECT 用命令携带的代次，代次未变时沿用已知 N，代次变化则 N 未知直到下一个开启 OK。N 未知时 NEXT/PREV 被忽略，因为无法环形步进。
- **切歌清理顺序**（B11）：游标前进，分配新流序号并写请求信箱；播放模块放弃旧流（Platform Stop）；归还手中旧块；队列里的旧序号块取出即归还；新流 OK 后重新开流、首帧、预填、Start。自动前进走同一路径。
- **暂停**：暂停 = 播放模块暂停输出，流保持。暂停中换曲在开启 OK 后 `BeginStream` 再立即暂停输出，使推进只解首帧并预填，不出声（B12）；开启应答到达之前就被暂停的流同样处理。
- **播完**（B13、B14）：流记录 ENDED 且手中无块、队列无本流的块时告知播放模块输入读尽（只一次）；播放模块回报流尾放完后游标前进（末曲回首曲），不出现停止态。短文件按同一条流记录依次处理。
- **块的持有**（B15）：同一时刻最多持一块，字节全部拷进窗口后立即归还；块比窗口大时手中保持，余量下一轮继续交。
- **快照发布**（B17）：每解出一帧更新 `PositionMs`，其他字段一变化即整份发布；没有变化不发布。总时长在接受切歌命令的同一刻变为未知，首帧给出后才填。位置只在切歌或重新开播时回 0。

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

- **坏文件跳过**（B5）：`SkippedBadCount` 每次 +1；沿最近一次移动的方向前进（PREV 向前，NEXT、SELECT、停止态 TOGGLE、播完前进向后），暂停态保持暂停。连跳计数达到最近一次开启 OK 带回的 N 即停止 ALL_BAD（游标保留，跳过数增加 N）。计数在输出真正 Start（`OutputStarted`）时清零，不在首帧解出或开启 OK 时清零；用户换曲命令与进入停止态也清零。不在首帧清零，是因为 Start 被拒可能出现在比首帧更晚的一次 `Pump` 里，否则整库采样率都被拒的曲目会无界轮转到不了 ALL_BAD（B5 与 B6 合起来的缺口）。N 未知（开机后第一首就是坏文件，且应答没有带回曲目数）时无法计数，按 NO_TRACK 停止；BAD_FILE 应答若带回代次与曲目数（Storage 目前填 0），状态机采用它们。
- **T_data**（B7，`PLAYBACK_MACHINE_DATA_TIMEOUT_MS`，初值 2000）：只在“需要数据却没有”时计时——等开启应答（从写请求起算，暂停中也算）；或本流未终止、播放模块补半区时回报输入不够、手中无块且队列没有本流的块（B16，预填、断流、欠载后的补满）。收到本流的应答或块重新计时；暂停且输出已满、排空（ENDED 之后）、停止时播放模块不要输入，自然不计时。
- **T_out**（B8，`PLAYBACK_MACHINE_OUTPUT_TIMEOUT_MS`，初值 500）：`OutputStarted` 起算，每次 `OutputProgress` 重新计时；暂停、放弃流、流尾放完、欠载（播放模块已 Stop）时停止计时，直到下一次 `OutputStarted`；恢复输出成功即视为 Start 并从恢复时刻起算。到期与 TRANSFER_ERROR 同路。判定放在本次推进之后，迟到的唤醒不会把已经发生的半区释放误判为无进展。到期都是“经过时间达到阈值”（≥），时刻按差值比较，回绕安全。
- **AUDIO_FAULT 锁存**（B10、K3）：之后停止态的 TOGGLE、SELECT 不开流、不动游标，只让停止次数 +1；NEXT/PREV 只移动游标。切歌途中 Stop 失败时，已写的新流请求被“无流”覆盖，游标停在已前进的位置。因其他原因停止时 Stop 失败，原因升级为 AUDIO_FAULT（停止次数仍只 +1）。锁存期间没有流，所以拔卡、代次变化没有可处理的流事实；重启才解除，不自动复位 MCU。
- **R**：同步预算是播放模块的配置常量（`PLAYBACK_SERVICE_SYNC_BUDGET_BYTES`），T_data、T_out 在 `playback_task_config.h`。

## 与外部的接缝

状态机不包含 Storage 或 FreeRTOS 的头，外部依赖都经 `Playback_MachineOpsTypeDef` 注入，由 `playback_task.c` 装配：

- 存储窗口：写请求信箱、窥视流记录、窥视/取走就绪块、归还块。类型与 `storage_audio_stream.h` 的请求、记录、结果枚举一一对应，外壳做 1:1 映射并以静态断言固定。
- 播放模块：`BeginStream`、`Feed`、`FinishInput`、`Pump`、`PauseOutput`、`ResumeOutput`、`AbandonStream`，签名即 `Service_Playback_*`，外壳直接填函数指针。
- 状态信箱：`PublishSnapshot` 覆盖写。

## 编译期依赖

- 状态机（`playback_machine.*`）：`playback_contract.h`、`playback_task_config.h`、`Service/playback/playback_service.h`（只取 `Service_Playback_PumpReportTypeDef` 与 `Service_StatusTypeDef`）。不依赖 FreeRTOS、HAL、`APP/tasks/storage/`。
- 任务外壳（`playback_task.c`）：状态机、`APP/tasks/storage/stream/` 的 `storage_audio_stream.h` 与 `storage_audio_block.h`、`Service/playback/`、`Service/log/`、`Platform/audio/` 的公开 Interface，以及 FreeRTOS 任务、队列与通知 Interface。不依赖 HAL、FatFs、LVGL。

## 主机测试

`Tests/playback_task/`：假存储窗口与假播放模块，覆盖 K3 全部格子、K5 的快照字段、流身份与块交接、切歌清理顺序、暂停、EOF 与短文件、游标环形步进与代次绑定，以及 B5 坏文件有界跳过、B6 每一行停止原因、B7/B8 超时、B10 与 K3 的 AUDIO_FAULT 锁存。
