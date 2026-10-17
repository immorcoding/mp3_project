# Playback Task 主机测试

本目录通过 `playback_machine.h` 的公开接口验证播放任务的纯 C 状态机（[#71](https://github.com/immorcoding/mp3_project/issues/71) 缝 1，票 [#78](https://github.com/immorcoding/mp3_project/issues/78)）：编译真实的 `APP/tasks/playback/playback_machine.c`，不链接 FreeRTOS、HAL、Storage 或真实播放模块。外部依赖由 `playback_task_test_support.c` 的替身经 `Playback_MachineOpsTypeDef` 注入：

- **假存储窗口**：请求信箱（记录写入）、流记录信箱（测试脚本化 Storage 一侧）、就绪块队列与块的取走/归还账（统计同时持有块数，B15 最多一块）；
- **假播放模块**：按窗口容量、每帧字节数和每次推进帧数建模的 `Service_Playback_*` 子集，暂停且预填已满时不再解码，`fake_player_release_tail()` 模拟“流尾放完”；
- **调用日志**：替身按序记录调用，用来断言 B11 的清理顺序。

断言只看公开可观察的行为：GUI 读到的快照（最近一次发布的内容）、请求信箱写入、块归还、对替身的调用序列。时间由测试传入，不依赖时间偶然性。

四个 CTest：

| CTest | 覆盖 |
|---|---|
| `playback_task_commands` | 开机全零快照；K3 命令表在播放、暂停、停止三态下的全部格子（含 SELECT“选中并播放”、暂停中 NEXT/PREV 保持暂停并静默预填、无游标时 NEXT/PREV 忽略）；开启 OK、首帧与逐帧位置、总时长在切歌时变未知；`HandledCommandSeq` 回显（含被忽略的命令）；连按 N 次游标走 N 步；无变化不发布 |
| `playback_task_cursor` | 游标环形步进与代次绑定：首次开播从 0 起并绑定应答代次、请求带游标代次与新位置、单曲库回到自己、开启 OK 前曲目数未知时忽略步进、SELECT 同代次沿用曲目数、换代次重新绑定、流序号严格单调（覆盖原 `Tests/storage_catalog` 游标用例的行为，旧用例随游标退场票删除） |
| `playback_task_stream` | B3/B15：旧序号应答与旧块丢弃、新流的块在开启 OK 前留在队列、块拷完即还、字节顺序保持、块比窗口大时手中保持、同一时刻最多一块；B11 切歌清理顺序（写请求 → 放弃旧流 → 归还手中块）；新流开启 OK 后才重新开流 |
| `playback_task_stream_end` | B13/B14：ENDED 之后全部拷完才告知读尽且只一次、没有 ENDED 不告知；流尾放完后自动前进（末曲回首曲、不出现停止态、只前进一次）；短文件在一次唤醒内按序处理；位置单调且切歌清零；B12 排空期间与曲间的暂停 |

运行（仓库根目录）：

```powershell
./scripts/test-host.ps1 -Module playback_task
```

## 不覆盖

下一票（#79）的坏文件跳过、T_data/T_out、故障与停止原因表；任务外壳（命令 FIFO、状态信箱、FLAG、回调注册）归 #80；播放模块自身行为见 `Tests/playback_service/`。Host PASS 不是板级验收，出声、Cache、时钟与优先级抢占只能上板。
