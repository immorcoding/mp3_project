# 播放任务

播放任务持有唯一的播放状态机：它是播放游标的唯一写者，向 Storage 写“想要的流”，把 Storage 交来的音频块交给 `Service_Playback_*` 解码与输出，并发布给 GUI 的播放快照。所有权按 [ADR-0020](../../../docs/adr/0020-playback-state-machine-ownership.md)，来源是 [#71](https://github.com/immorcoding/mp3_project/issues/71) 的 K1–K5、B1–B17。

本目录当前只落地了**公开合同**与**纯 C 状态机**（[#78](https://github.com/immorcoding/mp3_project/issues/78)）。任务外壳（`playback_task.c/.h`、通知槽、命令 FIFO、状态信箱、`playback_command_send()` 与 `playback_snapshot_read()` 的实现、事件回调注册）归任务外壳票（#80），坏文件跳过、T_data/T_out 超时、播放模块与 Platform 故障、AUDIO_FAULT 锁存和停止原因表归下一票（#79）。

## 公开 Interface

- `playback_contract.h`：GUI 音乐分区与模拟器脚本化后端共用的唯一合同，只含类型（`Playback_CommandTypeDef`、`Playback_SnapshotTypeDef` 等）与 `playback_command_send()`、`playback_snapshot_read()` 两个声明，字段与 #71 K1 逐字一致，不暴露 FreeRTOS 句柄。GUI 里只有音乐分区（`APP/tasks/gui/music`）可以包含它，`Service/gui` 不得包含。真实后端与模拟器的假后端实现同一个头，在链接时互换。
- `playback_machine.h`：状态机的 Ops 与两个入口，只服务播放任务外壳，不是跨模块 Interface。
  - `playback_machine_init(machine, ops)`：快照全零（停止、无当前曲），不发布。
  - `playback_machine_step(machine, commands, command_count, now_ms)`：一次唤醒的全部工作。命令由外壳从命令 FIFO 取空（≤ 8 条）后传入；`now_ms` 是外壳传入的单调毫秒时刻（B9），状态机不读 tick、不依赖 FreeRTOS。

## 状态机的一次唤醒

顺序按 B1：读流记录（只认当前流序号，旧序号的丢弃，B3）→ 开启 OK 则让播放模块开新流 → 归还手中的旧序号块与就绪队列队首连续的旧序号块（≤ 32，B15）→ 逐条处理命令（K3，序号回显含被忽略的命令）→ 把手中块的剩余字节交给播放模块 → 推进输出管线（`Service_Playback_Pump()`），输入不够就继续交字节，没有进展即结束本次唤醒去等块。

- **命令表**：TOGGLE 按自身状态决定方向；NEXT/PREV 在播放中环形步进并切歌、在暂停中换曲保持暂停、在停止态只移动游标，无游标（或曲目数未知）时忽略；SELECT 在三态下都切到该曲播放，选中当前曲也换新流序号。
- **游标**：就是快照里的 `{HasTrack, Generation, Index}`，加上最近一次开启 OK 带回的曲目数 N。停止态无游标时按播放打开 `{代次 0, 位置 0}`；开启 OK 把代次换成应答代次并更新 N。NEXT/PREV 用游标的代次；SELECT 用命令携带的代次，代次未变时沿用已知 N，代次变化则 N 未知直到下一个开启 OK。N 未知时 NEXT/PREV 被忽略，因为无法环形步进。
- **切歌清理顺序**（B11）：游标前进，分配新流序号并写请求信箱；播放模块放弃旧流（Platform Stop）；归还手中旧块；队列里的旧序号块取出即归还；新流 OK 后重新开流、首帧、预填、Start。自动前进走同一路径。
- **暂停**：暂停 = 播放模块暂停输出，流保持。暂停中换曲在开启 OK 后 `BeginStream` 再立即暂停输出，使推进只解首帧并预填，不出声（B12）；开启应答到达之前就被暂停的流同样处理。
- **播完**（B13、B14）：流记录 ENDED 且手中无块、队列无本流的块时告知播放模块输入读尽（只一次）；播放模块回报流尾放完后游标前进（末曲回首曲），不出现停止态。短文件按同一条流记录依次处理。
- **块的持有**（B15）：同一时刻最多持一块，字节全部拷进窗口后立即归还；块比窗口大时手中保持，余量下一轮继续交。
- **快照发布**（B17）：每解出一帧更新 `PositionMs`，其他字段一变化即整份发布；没有变化不发布。总时长在接受切歌命令的同一刻变为未知，首帧给出后才填。位置只在切歌或重新开播时回 0。

## 本票之外（下一票接入）

- 开启结果 NOT_READY/STALE/NO_TRACK/BAD_FILE/IO_ERROR、终止 CARD_REMOVED、播放模块的同步预算耗尽与 Start 被拒、T_data/T_out、TRANSFER_ERROR、STOP_FAILED、AUDIO_FAULT 锁存规则、坏文件连跳计数与 `SkippedBadCount`：状态机目前对这些事实不作反应，快照里对应字段保持 0。
- 流中 READ_ERROR 是本票里唯一的停止出口，用来让命令表的停止态一列可测；其余停止原因共用同一个 `enter_stopped()`，由下一票接入。

## 与外部的接缝

状态机不包含 Storage 或 FreeRTOS 的头，外部依赖都经 `Playback_MachineOpsTypeDef` 注入：

- 存储窗口：写请求信箱、窥视流记录、窥视/取走就绪块、归还块。类型与 `storage_audio_stream.h` 的请求、记录、结果枚举一一对应，外壳做 1:1 映射。
- 播放模块：`BeginStream`、`Feed`、`FinishInput`、`Pump`、`PauseOutput`、`ResumeOutput`、`AbandonStream`，签名即 `Service_Playback_*`，外壳直接填函数指针。
- 状态信箱：`PublishSnapshot` 覆盖写。

## 编译期依赖

`playback_contract.h`、`playback_task_config.h`、`Service/playback/playback_service.h`（只取 `Service_Playback_PumpReportTypeDef` 与 `Service_StatusTypeDef`）。不依赖 FreeRTOS、HAL、`APP/tasks/storage/`。

## 主机测试

`Tests/playback_task/`：假存储窗口与假播放模块，覆盖 K3 全部格子、K5 的快照字段、流身份与块交接、切歌清理顺序、暂停、EOF 与短文件、游标环形步进与代次绑定。
