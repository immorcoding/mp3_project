# Playback Task 主机测试

本目录通过 `playback_machine.h` 的公开接口验证播放任务的纯 C 状态机（[#71](https://github.com/immorcoding/mp3_project/issues/71) 缝 1，票 [#78](https://github.com/immorcoding/mp3_project/issues/78)）：编译真实的 `APP/tasks/playback/playback_machine.c`，不链接 FreeRTOS、HAL、Storage 或真实播放模块。外部依赖由 `playback_task_test_support.c` 的替身经 `Playback_MachineOpsTypeDef` 注入：

- **假存储窗口**：请求信箱（记录写入）、流记录信箱（测试脚本化 Storage 一侧）、就绪块队列与块的取走/归还账（统计同时持有块数，B15 最多一块）；
- **假播放模块**：按窗口容量、每帧字节数和每次推进帧数建模的 `Service_Playback_*` 子集，暂停且预填已满时不再解码，`fake_player_release_tail()` 模拟“流尾放完”；故障注入（R 耗尽、Start 被拒、TRANSFER_ERROR、STOP_FAILED 等 `Pump` 事实，输出已 Start/有进展/欠载事件，下一次 Stop 失败、Pump 失败、BeginStream 被拒）；
- **调用日志**：替身按序记录调用，用来断言 B11 的清理顺序。

断言只看公开可观察的行为：GUI 读到的快照（最近一次发布的内容）、请求信箱写入、块归还、对替身的调用序列。时间由测试传入，不依赖时间偶然性。

八个 CTest：

| CTest | 覆盖 |
|---|---|
| `playback_task_commands` | 开机全零快照；K3 命令表在播放、暂停、停止三态下的全部格子（含 SELECT“选中并播放”、暂停中 NEXT/PREV 保持暂停并静默预填、无游标时 NEXT/PREV 忽略）；开启 OK、首帧与逐帧位置、总时长在切歌时变未知；`HandledCommandSeq` 回显（含被忽略的命令）；连按 N 次游标走 N 步；无变化不发布 |
| `playback_task_cursor` | 游标环形步进与代次绑定：首次开播从 0 起并绑定应答代次、请求带游标代次与新位置、单曲库回到自己、开启 OK 前曲目数未知时忽略步进、SELECT 同代次沿用曲目数、换代次重新绑定、流序号严格单调（取代已删除的 Storage 游标用例） |
| `playback_task_stream` | B3/B15：旧序号应答与旧块丢弃、新流的块在开启 OK 前留在队列、块拷完即还、字节顺序保持、块比窗口大时手中保持、同一时刻最多一块；B11 切歌清理顺序（写请求 → 放弃旧流 → 归还手中块）；新流开启 OK 后才重新开流 |
| `playback_task_stream_end` | B13/B14：ENDED 之后全部拷完才告知读尽且只一次、没有 ENDED 不告知；流尾放完后自动前进（末曲回首曲、不出现停止态、只前进一次）；短文件在一次唤醒内按序处理；位置单调且切歌清零；B12 排空期间与曲间的暂停 |
| `playback_task_bad_files` | B5：BAD_FILE、R 耗尽、Start 被拒都跳过并提示（`SkippedBadCount` +1）；方向（PREV 向前，NEXT/SELECT/停止态 TOGGLE 向后）；输出 Start 清零而非首帧或开启 OK 清零；首帧与 Start 被拒不论同一回报或更晚的回报都累计到 ALL_BAD；N 上界、N = 1、整轮 ALL_BAD（跳过数 +N、停止次数 +1、游标保留）；用户换曲命令与进入停止态清零；暂停中跳过保持暂停；N 未知按 NO_TRACK；旧流应答丢弃；两个计数单调 |
| `playback_task_stop_reasons` | B6 停止各行：IO_ERROR/READ_ERROR 保留游标，NOT_READY/STALE/NO_TRACK/CARD_REMOVED 重置游标；都不计入连跳；无卡按播放后可重试；停止放弃流、归还块、写“无流”；旧流终止事实丢弃；停止次数每次 +1 |
| `playback_task_timeouts` | B7 T_data：等应答从写请求起算、应答/块重新计时、到期那次唤醒收到块不误停、欠载后的补满、暂停中换曲计时、暂停且输出已满/排空/停止不计时、播完前进后重新起算、回绕；B8 T_out：Start 起算、释放重新计时、预填与暂停不计时、恢复后起算、与 T_data 互不替代 |
| `playback_task_faults` | TRANSFER_ERROR 与推进失败 → AUDIO_ERROR 可重试；STOP_FAILED → AUDIO_FAULT 锁存；锁存后 TOGGLE/SELECT 只让停止次数 +1、NEXT/PREV 只移动游标；切歌途中 Stop 失败时新流请求被“无流”覆盖；暂停、其他停止、T_out 到期时 Stop 失败升级为 AUDIO_FAULT；`BeginStream` 被拒 |

运行（仓库根目录）：

```powershell
./scripts/test-host.ps1 -Module playback_task
```

## 不覆盖

任务外壳（命令 FIFO、状态信箱、FLAG、回调注册）归 #80；播放模块自身行为见 `Tests/playback_service/`。Host PASS 不是板级验收，出声、Cache、时钟与优先级抢占只能上板。
