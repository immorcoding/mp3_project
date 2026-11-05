# Playback Task 主机测试

本目录通过 `playback_machine.h` 的公开接口验证播放任务的纯 C 状态机（[#71](https://github.com/immorcoding/mp3_project/issues/71) 缝 1，票 [#78](https://github.com/immorcoding/mp3_project/issues/78)）：编译真实的 `APP/tasks/playback/playback_machine.c`，不链接 FreeRTOS、HAL、Storage 或真实播放模块。外部依赖由 `playback_task_test_support.c` 的替身经 `Playback_MachineOpsTypeDef` 注入：

- **假存储窗口**：请求信箱（记录写入）、流记录信箱（测试脚本化 Storage 一侧）、就绪块队列与块的取走/归还账（统计同时持有块数，B15 最多一块）；
- **假播放模块**（含线性假 `MapSeek`/`ReopenStream`：按 `BeginStream` 的音频字节数与首帧给出的总时长线性映射，最多钳到 `AudioBytes − 2 × BytesPerFrame`，首帧后流级事实保留到下一次 `BeginStream`）：按窗口容量、每帧字节数和每次推进帧数建模的 `Service_Playback_*` 子集，暂停且预填已满时不再解码，`fake_player_release_tail()` 模拟“流尾放完”；故障注入（R 耗尽、Start 被拒、TRANSFER_ERROR、STOP_FAILED 等 `Pump` 事实，输出已 Start/有进展/欠载事件，下一次 Stop 失败、Pump 失败、BeginStream 被拒）；
- **调用日志**：替身按序记录调用，用来断言 B11 的清理顺序。

断言只看公开可观察的行为：GUI 读到的快照（最近一次发布的内容）、请求信箱写入、块归还、对替身的调用序列。时间由测试传入，不依赖时间偶然性。

十一个 CTest：

| CTest | 覆盖 |
|---|---|
| `playback_task_commands` | 开机全零快照；K3 命令表在播放、暂停、停止三态下的全部格子（含 SELECT“选中并播放”、暂停中 NEXT/PREV 保持暂停并静默预填、无游标时 NEXT/PREV 忽略）；开启 OK、首帧与逐帧位置、总时长在切歌时变未知；`HandledCommandSeq` 回显（含被忽略的命令）；连按 N 次游标走 N 步；无变化不发布 |
| `playback_task_cursor` | 游标环形步进与代次绑定：首次开播从 0 起并绑定应答代次、请求带游标代次与新位置、单曲库回到自己、开启 OK 前曲目数未知时忽略步进、SELECT 同代次沿用曲目数、换代次重新绑定、流序号严格单调（取代已删除的 Storage 游标用例） |
| `playback_task_stream` | B3/B15：旧序号应答与旧块丢弃、新流的块在开启 OK 前留在队列、块拷完即还、字节顺序保持、块比窗口大时手中保持、同一时刻最多一块；B11 切歌清理顺序（写请求 → 放弃旧流 → 归还手中块）；新流开启 OK 后才重新开流 |
| `playback_task_stream_end` | B13/B14：ENDED 之后全部拷完才告知读尽且只一次、没有 ENDED 不告知；流尾放完后自动前进（末曲回首曲、不出现停止态、只前进一次）；短文件在一次唤醒内按序处理；位置单调且切歌清零；B12 排空期间与曲间的暂停 |
| `playback_task_bad_files` | B5：BAD_FILE、R 耗尽、Start 被拒都跳过并提示（`SkippedBadCount` +1）；方向（PREV 向前，NEXT/SELECT/停止态 TOGGLE 向后）；输出 Start 清零而非首帧或开启 OK 清零；首帧与 Start 被拒不论同一回报或更晚的回报都累计到 ALL_BAD；N 上界、N = 1、整轮 ALL_BAD（跳过数 +N、停止次数 +1、游标保留）；用户换曲命令与进入停止态清零；暂停中跳过保持暂停；N 未知按 NO_TRACK；旧流应答丢弃；两个计数单调 |
| `playback_task_stop_reasons` | B6 停止各行：IO_ERROR/READ_ERROR 保留游标，NOT_READY/STALE/NO_TRACK/CARD_REMOVED 重置游标；都不计入连跳；无卡按播放后可重试；停止放弃流、归还块、写“无流”；旧流终止事实丢弃；停止次数每次 +1 |
| `playback_task_timeouts` | B7 T_data：等应答从写请求起算、应答/块重新计时、到期那次唤醒收到块不误停、欠载后的补满、暂停中换曲计时、暂停且输出已满/排空/停止不计时、播完前进后重新起算、回绕；B8 T_out：Start 起算、释放重新计时、预填与暂停不计时、恢复后起算、与 T_data 互不替代 |
| `playback_task_seek` | #176 跳转：播放中执行（落点同一步进快照并回显、新流序号与 `AudioOffset`、B11 顺序、不改三态/`StopCount`/`DurationMs`、重开而非 `BeginStream`）；Seek INFO 字段与 `lat`（假 DWT）、`map=toc|lin`、`clamp`；四种静默忽略（无曲、停止态、曲目不符、总时长 0、映射失败）只记 DEBUG 且推进序号；同批连续 SEEK 只执行最后一条而中间隔命令不合并；跨唤醒新跳转作废旧流的应答与块；旧流位置不顶回落点（含向回跳变小）；暂停中跳转保持 PAUSED、重开后暂停、预填完成时记 INFO；越尾播完前进与暂停中停在末尾（含钳位后零帧：暂停中不前进、恢复后前进，PLAYING 下零帧直接前进；假播放模块在重开后读尽零帧时按真实模块的 NO_AUDIO 回报流尾放完）；`TrackStartCount` 的加一与不加；NEXT/停止使跳转与 Seek INFO 作废；普通开流 `AudioOffset` 为 0；T_data/T_out 与切歌一致；Stop 失败与重开失败按 AUDIO_FAULT |
| `playback_task_switch` | #204 切歌到出声：NEXT、PREV、SELECT 在输出重新起播时记一行 `Switch idx=N lat=U`（序号是新曲，lat 来自假 DWT，100 周期折合 1 µs）；起播前不记、起播后只记一次；开播 TOGGLE、跳转和暂停中换曲不记；起播前停止作废；起播前又切歌以最后一次为准、从最后一条命令算起；开机以来欠载累计 `playback_machine_underrun_total()` 累加每次回报 |
| `playback_task_faults` | TRANSFER_ERROR 与推进失败 → AUDIO_ERROR 可重试；STOP_FAILED → AUDIO_FAULT 锁存；锁存后 TOGGLE/SELECT 只让停止次数 +1、NEXT/PREV 只移动游标；切歌途中 Stop 失败时新流请求被“无流”覆盖；暂停、其他停止、T_out 到期时 Stop 失败升级为 AUDIO_FAULT；`BeginStream` 被拒 |

`playback_task_log` 另用一个有序记录文本的 `LogInfo` 替身（`fake_log_*`），断言 INFO 事件的顺序与文案（#108）：播放、切歌、暂停、恢复、停止的完整序列、停止原因文案与快照一致、坏文件跳过、播完与自动前进、被忽略的命令与空闲唤醒不记日志。

运行（仓库根目录）：

```powershell
./scripts/test-host.ps1 -Module playback_task
```

## 不覆盖

任务外壳（命令 FIFO、状态信箱、FLAG、回调注册）归 #80；播放模块自身行为见 `Tests/playback_service/`。Host PASS 不是板级验收，出声、Cache、时钟与优先级抢占只能上板。
