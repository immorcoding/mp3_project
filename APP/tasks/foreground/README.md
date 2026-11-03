# Foreground Task（spike）

**这是 [#191](https://github.com/immorcoding/mp3_project/issues/191)「上板验证：加锁、前台任务、GUI 睡眠」的可丢弃固件，分支 `spike/app-tasks`，不合入。** 目的只有一个：在板上拿到后续决定要用的实测数字。音乐的用户行为不变，Storage 主循环、名称槽、窗口槽、曲库都留在原位，不迁移；不拆音频读取、卷、后台任务，不改任务优先级。

前台任务（任务名 `Foreground Task`，优先级等于 GUI，1536 word 栈，`xTaskCreateStatic` 创建，栈和 TCB 在默认 `.bss`，即 DTCM）在音乐处于播放状态时自动按阶段给系统施加负载，并每 10 s 汇总一次日志。暂停或停止时等待，阶段计时只在播放中前进。

## 这次改了什么

- **GUI 睡眠**（[#190](https://github.com/immorcoding/mp3_project/issues/190)）：`Service_GUI_Process()` 返回 1 至 16 ms，`gui_task` 在 DWT 测量段之外 `vTaskDelay`；运行时开关让 GUI 不睡，P0 用它测基线。
- **Filesystem 加锁**：公开入口拿一次锁、返回前放，SD 与 Flash 执行器去掉任务身份绑定，完成通知改为每卷一个二值信号量加事件变量。默认全局一把；`SPIKE_LOCK_PER_VOLUME` 为每卷一把加一把短的打开表锁，同时 `ffconf.h` 的 `_USE_LFN` 变为 2。详见 [Service/filesystem/README.md](../../../Service/filesystem/README.md#锁spike191)。
- **`ui_cell`**（[#193](https://github.com/immorcoding/mp3_project/issues/193) 草案）：`APP/ui_cell/`，一把藏在模块内的互斥锁、序号、不对外给锁、GUI 用的 try 版本。只在 P4 的合成场景里用，音乐不走它。
- **播放任务**：新增一行 `Switch idx= lat=`（用户换曲命令被处理到输出重新起播），欠载次数在上层累加。
- **Storage**：只在拔卡路径上多记几个时刻（`spike_unplug.c`），行为不变。

## 构建两个固件

仓库根目录执行，两个固件各用独立构建目录（切换选项后不要复用同一个目录）：

```powershell
# 全局一把锁（默认）：build/firmware-debug/mp3_project.hex
cmake --preset firmware-debug
cmake --build build/firmware-debug

# 每卷一把锁：build/firmware-debug-pervol/mp3_project.hex
cmake --preset firmware-debug -B build/firmware-debug-pervol -DSPIKE_LOCK_PER_VOLUME=ON
cmake --build build/firmware-debug-pervol
```

启动后第一条 `SPIKE INIT lock=GLOBAL|PER_VOLUME` 会写明当前是哪一个，日志对照时先看它。

## SD 卡要放什么

- `Music/`：平时的曲库，几首 MP3 就行，要能连续播放 4 分钟以上（循环播放也可以）。
- `Spike/big.txt`：**手动放**，至少 5 MB 的文本，要有换行（任意文本重复拼接即可）。缺失时 P2、P5 打一行 `UNTESTED` 并跳过。
- Flash 卷不用准备，P3 会在里面建 `Spike/` 和临时文件 `Spike/load.bin`，用完即删。Flash 卷未就绪时 P3 同样打 `UNTESTED` 并跳过。

## 上板步骤

1. 用 DFU 烧录一个固件（外部 Loader 已装好），插好带 `Spike/big.txt` 的卡，开串口日志。
2. 开机后在界面里按播放。前台任务在音乐处于播放状态时开始，P0 至 P4 自动进行，约 3.5 分钟（30 + 30 + 60 + 60 + 30 s）：

| 阶段 | 时长 | GUI | 前台任务做什么 |
|---|---|---|---|
| P0 | 30 s | 不睡 | 无负载（基线） |
| P1 | 30 s | 睡 | 无负载 |
| P2 | 60 s | 睡 | 读大 TXT 模拟分页：顺序读 32 KiB 块并统计换行，每 8 块在随机页起点读 4 KiB |
| P3 | 60 s | 睡 | Flash 卷：每次写 64 KiB 并同步，写到约 1 MiB 后删除，再回收。开始时按 FTL 回收水位算出要写多少空闲块才会让回收真擦除（见下文“Flash 回收怎么触发”），写到水位以下后回收才会擦除。固定跑满 60 s，这样才有音频读取样本和手动切歌的时间；真擦除次数只统计，不决定结束时间 |
| P4 | 30 s | 睡 | `ui_cell` 合成拷贝：每 16 ms 写 6 KiB，GUI 每圈读（合成场景，结果仅作参考） |
| P5 | 不限 | 睡 | 持续读 TXT，等手动操作 |

3. P5 开始后（日志出现 `PHASE start P5`）手动做三件事，每件之间留 15 s 以上：
   - **拔卡**：直接拔出 SD 卡，看 `UNPLUG via=… ` 几行和 `SD: Card removed (via state|presence).`；需要时再插回去（前台任务每 500 ms 重新打开 TXT），插回后应是 `Card inserted`、`Filesystem mounted.`，并且音乐能再播。拔卡时前台任务有读取在途最能暴露上一轮的问题，请拔 2 至 3 次。
   - **切歌和翻页**：连按下一首、在 Queue 页滑动，看 `Switch`、`LOCK`、`UNDER`。
   - **切到非唱盘页面**：从 Now Playing 滑到别的页面停 30 s，对比这段窗口的 `GUI ... flush=`；切回来再对比一次。
4. 换另一个固件重复一遍。两个固件的 P0 与 P1 是同一个对照（GUI 不睡与睡），P2 与 P3 的差别是“锁粒度”。

## 日志行与票上数字

标签统一 `SPIKE`，行首第一个词固定，每行不超过 127 B。窗口汇总每 10 s 一次，阶段结束再出一次（`PHASE end` 之前）。任务名里的空格在 `LOCK` 行里写成下划线。

| 票上的数字 | 日志行 | 怎么读 |
|---|---|---|
| 各任务栈高水位 | `MONITOR` 标签的 `<任务名>: total=…, min free=…, peak=…` | 现有 Monitor 行，已含 `Foreground Task`；每 5 s 一轮 |
| CPU 空闲比例 | `SPIKE CPU <阶段> win=…ms cnt/ms=… idle=… gui=… fg=… sto=… pb=… log=… mon=… tmr=… oth=…` | FreeRTOS 运行时统计的窗口差值，单位 %。**不是 LVGL 浮层的 CPU%**。`cnt/ms` 是运行时计数器每毫秒的计数，TIM5 预分频 4799、定时器时钟 240 MHz 时应为 50.0（由时钟树推出，未实测），32 位约 23.9 小时回绕 |
| SD 读 32 KiB 耗时（音频流） | `SPIKE SDREAD <阶段> audio32K n=… min=… avg=… max=…us` | Storage Task 读一整块音频（32 KiB）的持锁时长：拿到 SD 锁之后到 `ReadFile` 返回，不含等锁；每个阶段都有，按阶段清零，用来对比 P1（无负载）、P2（读 TXT）、P3（Flash 写）对音频读取的影响 |
| SD 读 32 KiB 耗时（前台 TXT） | `SPIKE SDREAD <阶段> fg32K n=… min=… avg=… max=…us` | 前台任务自己读 TXT 的 32 KiB，同样口径，只在 P2、P5 有数（文件尾的短读不计） |
| 随机页 4 KiB 读 | `SPIKE SDREAD <阶段> fg4K n=… avg=… max=… err=… last=… nl=… pass=…` | 同上；`err` 是读失败次数，`nl` 是已统计的换行数，`pass` 是读到文件尾回绕的次数 |
| 单次持锁最长、等锁最长分别是谁 | `SPIKE LOCK <锁> <ph|all> hold=<us>us@<任务>/<操作> wait=<us>us@<任务>/<操作> n=<次数>` | `ph` 是本阶段，`all` 是自启动；锁名在全局一把时是 `GLOBAL`，每卷一把时是 `SD`、`FLASH`、`TABLE`（打开表锁）。持锁者是拿锁的任务和操作，等锁者是等的那个任务和操作 |
| 等锁最长时是被谁挡住 | `SPIKE LOCKBLK <锁> ph longest_wait_blocked_by=<任务>/<操作>` | 尽力而为：取等锁开始那一刻的持锁者 |
| 切歌到出声的时延 | `PLAYBACK` 标签的 `Switch idx=<位置> lat=<us>` | 命令被播放任务处理到输出重新起播，微秒；边界日志，每次换曲一行 |
| 欠载次数 | `SPIKE UNDER <阶段> total=… win=…` | `total` 是开机以来累计（在播放状态机里累加，Platform 的计数每次 Start 清零），`win` 是本窗口增量 |
| Flash 写入与同步耗时 | `SPIKE FLASHIO <阶段> write64K|sync n=… min=… avg=… max=…us` | 持锁时长；只在 P3 有数。对比 P3 与 P2、P1 的 `SDREAD audio32K` 和 `UNDER` 看 Flash 写是否拖慢 SD 读 |
| 一次回收的实际耗时 | `SPIKE FLASHIO <阶段> reclaim_erase n=… min=… avg=… max=…us`（真擦除）与 `reclaim_idle …`（空转） | 口径：在同一次 Flash 卷锁内读回收前后 FTL 的 `SessionErases`，差值不为零就是真擦除（FTL 擦除并校验完一块后才加一），否则是空转。`reclaim_erase` 是票上要的数；`n=0` 说明这次没触发到水位 |
| 空闲块与失效块的前后值 | `SPIKE FLASHIO <阶段> blocks free=<开始>><现在> stale=<开始>><现在> valid=… low=… high=… reserve=…` 与 `FLASHIO <阶段> erases phase=… reclaim=… other=… real_calls=… idle_calls=… reached=… to_low=…` | `low`、`high` 是按 FTL 公式推算的回收水位（`PHASE start P3` 行里还有 `need`：还要消耗多少空闲块才到 `low`）。`erases` 的 `phase` 是本阶段擦除总数，`reclaim` 是回收调用里观察到的，`other` 是差值（写入路径里的前台回收，正常为 0）；`to_low` 为 0 以外说明时间到时还没到水位，请再跑一次 P3（空闲块的消耗在重启后保留） |
| 拔卡时序 | `SPIKE UNPLUG via=state|presence|insert detect_isr=0 debounce_end=+… stream_closed=+… catalog_invalid=+…`、`UNPLUG unmount_enter=+… unmount_done=+… unmount_status=…`、`UNPLUG fg_read status=… returned_at=+…ms in_lock=…us` | 全部是相对检测中断（第一个边沿，ISR 里只存 tick）的毫秒。`via` 说明拔出收尾由哪条判断触发：`state` 是 `Platform_SD_Process` 给出 REMOVED；`presence` 是事件被吞（传输在途时拔卡，设备层把状态直接写成 NOT_PRESENT），由“卡检测电平显示不在且卷还挂着”触发；`insert` 是只收到 INSERTED 而卷还挂着（换卡，或拔出被吞后插回）。卸载要先拿 SD 锁，`unmount_done` 与 `unmount_enter` 的差含等在途读取；`fg_read` 是前台任务拔卡后第一次失败的读取：状态、返回时刻、持锁多久（在途读取等 DMA 超时时可达数百毫秒）。3 s 内没有读取失败则写 `fg_read=none` |
| 插卡初始化持锁多久 | `SPIKE LOCK <SD 或 GLOBAL> ph|all …@Storage_Task/HotplugRefresh` | 热插拔刷新（无卡时 DeInit、插卡时 Init）现在在 SD 卷锁内执行，操作名 `HotplugRefresh`；在 `hold=` 里看到它就是插卡初始化的持锁时长 |
| 唱盘不在屏幕上时是否还刷屏 | `SPIKE GUI <阶段> win=…ms loops=… flush=… sleep_avg=…ms frame_max=…us sleep=on|off` | 窗口内 GUI 循环数、flush 次数、平均睡眠、最长一圈。`sleep=off` 只在 P0 |
| `ui_cell` 持锁时长 | `SPIKE CELL P4 write n=… avg=… max=…us (synthetic 6KiB)` 与 `read …` | 写和读各自的持锁平均与最大，合成场景 |
| GUI 拿不到锁的频率 | `SPIKE CELLGUI P4 try=… fail=… (synthetic, reference only)` | `try` 是序号变了、GUI 尝试拿锁拷贝的次数，`fail` 是其中没拿到的；仅作参考 |
| GUI 或播放任务误调文件接口 | `SPIKE GUARD <阶段> fs_calls_from_gui_or_playback=…` | 正常应为 0（非 0 才在窗口里输出，阶段结束一定输出）；首次发生时另有一条 WARN |

其余行：`SPIKE INIT …`（锁模式、`_FS_LOCK`、`_USE_LFN`）、`SPIKE PHASE start|end|skip|wait|pause|all_done …`。

拔卡后 Queue 清空的延迟、连滑和连按切歌的显示延迟**没有测**，票上记“未测”，转 0.10.0 板级验收。

## Flash 回收怎么触发（读代码所得，`Components/flash_ftl/flash_ftl.c`，未对照手册，除注明板测外未实测）

- **写路径里没有擦除**：写一个逻辑组（7 个 512 B 扇区，3584 B）= 取下一个空闲块（循环游标），编程头页和最多 14 个数据页（全 0xFF 的页跳过），每页编程后读回 256 B 校验，再编程提交页并读回校验，最后切换 RAM 映射、把旧版本所在块标成失效（不是空闲）。整组以下的写还要先读旧块 4 KiB。**只有空闲块数不高于 2（`FLASH_FTL_MIN_FREE_BLOCKS`）时，写入自己才会先做一次前台回收（擦一块）**。所以第一轮板测里 `write64K` 的 0.33 至 0.74 s（板测，平均约 0.63 s）是约 19 至 21 次组提交（64 KiB 除以 3584 B 约 18.3，再加 FAT 与目录扇区所在的组）的页编程加读回校验，每组约 17 至 36 ms，不含擦除。P3 的 `erases ... other=` 是验证：写入路径里的擦除应为 0。
- **回收有水位**：`ReclaimFlash` 受理一次 GC 步骤，`low = ceil(预留块 × 75%)`，`high = ceil(预留块 × 90%)`。空闲块数不高于 `low` 时置“要回收”，不低于 `high` 时清除；没有“要回收”就直接返回成功，不擦除——这就是第一轮板测里 96 次回收全部约 14 µs 的原因。要回收时按游标找下一个失效块，擦除、读回 8 × 512 B 校验后标成空闲，`SessionErases` 加一；**一次调用至多擦一块**。
- **本板的数**：FTL 分区 24 MiB = 6144 块，扣两个卷头得 6142 个数据块，预留块 `ceil(6142 × 10%)` = 615，逻辑容量 5527 组（38689 个扇区），所以 `low` = 462、`high` = 554（`PHASE start P3` 行里会给出算出的值，和这里对不上就是 FTL 参数变了）。每次写一个逻辑组消耗一个空闲块，旧版本变失效而不是空闲，所以**刚格式化的卷要先消耗约 5680 个空闲块（约 270 次 64 KiB 写）才会第一次擦除**；消耗在重启后保留（启动扫描只把已擦除的块算空闲），板上之前跑过的 P3 都在为这次累积。
- **P3 怎么做**：开始时读空闲块数，算出 `need` = 空闲块数 − `low`，写入之间只让出 1 ms 以最快消耗空闲块；每写完一个约 1 MiB 的文件、删除之后进入回收阶段：每次回收读前后 `SessionErases`，真擦除就继续回收（一个阶段至多 64 次），连续 4 次空转回到写入。P3 固定跑满 60 s；到时还没到水位就在 `erases ... to_low=` 里写出还差多少。`erases` 的 `other` 是本阶段不来自前台回收调用的擦除，包括音频流关闭时 Storage 主循环的定期回收，不只是写入路径。不动 FTL，不写满卷（逻辑上只占 1 MiB，且只碰 `Spike/load.bin`）。

## 读数时要知道的

- `cnt/ms`、时钟树推出的 TIM5 频率、`ui_cell` 的持锁时长等都是日志里直接给的数，没有板上独立核对过。
- `SDREAD 32K` 只统计整块 32768 B 的读，文件尾的短读不计入。
- 耗时用 DWT 周期数，区间超过 4 s 改按 tick（毫秒精度）；DWT 为 32 位，480 MHz 约 8.9 s 回绕。
- 前台任务和 GUI 同级，按 1 ms 时间片轮转，所以前台任务只做文件读写和一次 `memcpy`，不做 CPU 重活。
- 每行日志之后让出 6 ms，避免挤满 30 条的日志消息池；仍可能在同一时刻与 Monitor 的 13 行一起把池占满，此时 `PLAYBACK` 等日志可能丢失，看到缺行请重做那个窗口。
- `ui_cell` 的格子和 GUI 副本、32 KiB 读缓冲放在 AXI SRAM（现有的 `.lvgl_large_ram_array` 段）；合成数据源与 Flash 写入的数据源是 SDRAM 里只读的壁纸资源区，不另占内存。栈和 TCB 在 DTCM。

## 风险（上板前必须知道）

- **第一轮板测的发现，这轮只修了 spike 分支，产品代码路径没有**：前台任务读取时拔卡，设备层（`SDCard_FailTransfer`）发现卡不在，直接把状态写成 NOT_PRESENT；消抖结束后 `Platform_SD_Process` 比较前后状态相同，不给 REMOVED，于是音频流不关、卷不卸载；插回才得到 INSERTED，此时旧卷还挂着，遗留的打开对象让 `MountSD` 返回 BUSY（主机测试复现了这一点），Storage 认为卷未挂载，音乐不可用。这轮在 `storage_sd_process()` 里补了三处：拔出判断不再只靠状态变化（卡检测电平显示不在且卷还挂着也走完整收尾）；只收到 INSERTED 而卷还挂着时先完整拔出收尾再挂载（卸载让所有任务的 Token 失效，所以挂载不再因别的任务开着文件而 BUSY）；热插拔刷新经 SD 卷锁执行。`Components/`、`Platform/` 没改，所以主线上同样的问题仍在，要等票里决定怎么修。

- 每卷一把锁时 `f_open`、`f_close`（写打开的文件先在表锁外 `f_sync`）、`f_opendir`、`f_unlink`、`f_rename`、`f_mount` 整个调用都包在打开表锁里，里面有磁盘读写。Flash 卷上的创建或删除会让 SD 上的打开等表锁，这正是要测的代价，但也意味着“每卷一把”并不能让所有操作互不影响。
- 全局一把锁时，Flash 擦除或 64 KiB 写入期间 Storage 读音频块要等锁，可能欠载；这是预期的对照。
- 卸载 SD 要先拿 SD 锁，拔卡时若前台任务有读取在途，卸载会等到那次读取返回（DMA 超时最多约 500 ms），期间音频流已关闭但卷还没卸。
- 前台任务和 GUI 同优先级，P2 的连续读取在全局锁下可能让 Storage 的音频读取等一个 32 KiB 读的时间；优先级继承会把前台任务临时抬到 2。
- 迟到的 SD DMA 完成事件问题（#188 第 7 条）没有解决：现在任何任务都可能在等，迟到事件可能被当成下一次传输的完成。
- `_FS_LOCK` 为 4，而文件槽也是 4：音频流、标签读取、前台的 TXT 或 Flash 文件同时打开时第五个对象打开会得到 `SERVICE_ERROR` 或 `SERVICE_BUSY`。
- 主机测试是单线程的，通过不等于并发没问题；两个固件都没有上板运行过。
