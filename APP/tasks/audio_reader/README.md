# 音频读取任务

音频读取任务只做一件事：读 SD 上的音频文件，填块池，到安全水位后让路；不解码（[ADR-0023](../../../docs/adr/0023-task-split-and-app-contract.md) 决定 4）。它从 Storage Task 的主循环里拆出来，成为优先级 3 的独立任务，所以 Flash 写入、曲库扫描、窗口和曲名读取都不再挡补块；播放侧合同（请求信箱、流记录信箱、块池、唤醒回调）的字段、枚举和语义与 0.9.0 完全相同，只改了名字，播放状态机不用改。决定来源 [#194](https://github.com/immorcoding/mp3_project/issues/194)（含 2026-11-04 对第 13 条 `SERVICE_BUSY` 的修正）、[#202](https://github.com/immorcoding/mp3_project/issues/202) D 节，卷事件与失败约定来自 [#195](https://github.com/immorcoding/mp3_project/issues/195) 与卷任务票 [#211](https://github.com/immorcoding/mp3_project/issues/211)。规则 ARC-12、ARC-17、STOR-8、STOR-12、DIAG-3、DIAG-5 至 DIAG-7。

用户可见的变化只有 #194 确认的三处：① 插卡或开机后曲库好之前点播放，立刻显示 “SD card not ready”（以前请求排队，2 s 内扫完才出声，否则 “Read timeout”），扫完要再点一次；② 读取任务真的卡死时 5 s 后才显示 “Read timeout”（以前 2 s）；③ 在途读取恰好被拔卡打断时显示 “No SD card”、游标重置（以前显示读取错误、游标保留，#211 已落实）。

## 目录

| 路径 | 内容 | 主机测试 |
|---|---|---|
| `audio_reader_task.c/.h` | Boot 里的内存准备 `audio_reader_boot_init()`、静态创建 `audio_reader_task_create()`、任务入口：唯一的等待点和每圈的固定顺序 | 否（`for(;;)` 没法测，只留创建和循环） |
| `audio_reader_stream.c/.h` | 请求信箱、流记录信箱、开流（越过标签、跳转偏移）、读一块、卷事件收尾、`SERVICE_BUSY` 重试；播放任务调用的四个函数 | 否（FatFs、信箱、盖章、任务通知，靠板级验收），每个判断都抽成下面的纯函数 |
| `audio_reader_block.c/.h` | 块池（SDRAM `.storage_audio_pool`）与空闲、就绪两个只传指针的静态队列 | 否 |
| `audio_reader_result.c/.h` | **失败结果加卷序号 → 写什么停止原因**：读块分类（继续、停住、重试、读错误）、开流分类、卷序号变了时该不该写 `CARD_REMOVED`、`SERVICE_BUSY` 日志节拍 | 是 |
| `audio_reader_pace.c/.h` | **决定等多久**：水位、让路、一直等、新请求与重试时刻的优先关系 | 是 |
| `audio_reader_diag.c/.h` | 两行新日志的格式化：`CARD_REMOVED lat=` 与 `SERVICE_BUSY` | 是 |
| `audio_reader_range.c/.h` | 越过标签与跳转偏移钳位（`AudioOffset`，ADR-0021），保留自 0.9.0 | 是 |
| `audio_reader_config.h` | 块大小与块数、段名、通知标志、水位与让路、重试间隔与日志节拍、定位符失效钩子开关 | — |

## 公开 Interface（播放侧合同，原样保留）

只改了名字：`storage_audio_*` → `audio_reader_*`，`Storage_Audio*TypeDef` → `AudioReader_*TypeDef`，`STORAGE_AUDIO_*` → `AUDIO_READER_*`；`.storage_audio_pool` 段名不改（只在 map 里看得到）。

- `audio_reader_stream.h`：`AudioReader_RequestTypeDef`（`StreamSeq`、`Generation`、`Position`、`AudioOffset`）、`AudioReader_OpenResultTypeDef`（`PENDING`、`OK`、`NOT_READY`、`STALE`、`NO_TRACK`、`BAD_FILE`、`IO_ERROR`）、`AudioReader_EndTypeDef`（`NONE`、`ENDED`、`READ_ERROR`、`CARD_REMOVED`）、`AudioReader_RecordTypeDef`；播放任务调用 `audio_reader_stream_request()`、`audio_reader_stream_get_record()`（非消费）、`audio_reader_stream_return_block()`、`audio_reader_stream_set_wake_callback()`。字段、枚举值和语义不变，`playback_task.c` 的 `_Static_assert` 照旧固定两边的大小、偏移和枚举值。
- `audio_reader_block.h`：`AudioReader_BlockTypeDef`（`StreamSeq`、`ValidBytes`、32 KiB 数据）、`audio_reader_block_peek_ready/take_ready`。新增 `audio_reader_block_ready_count()`（水位用，APP 内部）。
- 取消 `Storage_AudioMediaTypeDef`（#195：消抖由文件接口挡住，不需要介质状态入参）。曲库好不好改由曲库代次决定：代次 0（扫描中、卷变了、没发布）立刻应答 `NOT_READY`，读取任务不订阅“曲库就绪”（#194 第 15 条）。
- `audio_reader_task.h`：只给 `app_tasks.c`（Boot）用，不是跨模块 Interface。读取任务直接包含音乐曲库的公开头（`catalog.h`、`catalog_track_open.h`、`catalog_track_diag.h`），不做注入（ARC-23：任务包含应用的头不受分层脚本限制；读取任务只服务音乐，加 Books 不碰它）。

`playback_machine` 的 Ops 里存储窗口一组仍叫 `Storage`（`Playback_MachineStorageOpsTypeDef`、`Ops.Storage.*`）：播放状态机这次不改，这几个名字留给用户决定是否另开票改（未决问题，见交付汇报）。

## 优先级、栈与启动

| 任务 | 优先级 | 栈 | 创建方式 |
|---|---|---|---|
| 播放 | 4 | 5120 word，DTCM | 静态，不变 |
| **音频读取** | **3** | **1024 word（4 KiB），AXI `.app_axi_ram`** | 静态，TCB 在 DTCM `.bss`，名字 `Audio Read Task`（15 个字符，`configMAX_TASK_NAME_LEN` 为 16） |
| 卷、定时器服务 | 3 | 1024 word / 128 word | 不变 |
| 前台、GUI、Log、Monitor | 2 | 前台 1536 word（AXI），其余不变 | 前台见 [前台任务](../foreground/README.md)（#218：GUI、Log、Monitor 从 1 升到 2） |
| 后台、Flash 启动 | 1 | 不变 | Storage Task 已随 #220 删除，Flash 启动任务由 2 降到 1（与后台同级） |

`.app_axi_ram` 在本票时是 `0x4800` = 18432 B（Debug map：`0x2404b600` 至 `0x2404fe00`）：卷 4096、读取任务 4096、后台 6144、Flash 启动 4096，没有别的贡献者；#218 加前台任务栈 6144 和音乐的格子与 GUI 副本后是 0x9380 = 37760 B，见 [前台任务](../foreground/README.md)“栈、内存与段”。链接脚本、`FreeRTOSConfig.h` 一个字没改。HWD-2：NOLOAD 不清零，栈由 `xTaskCreateStatic` 整块填成已知值后才首次使用，`audio_reader_task.c` 有和卷任务、后台任务相同的 `_Static_assert`。栈只经 CPU 读写，不作 DMA，不需要 Cache 维护。**MPU**：`Core/Src/main.c` 只配了 SDRAM 一个 MPU 区域（Region 2，0xC0000000 起 32 MB），AXI SRAM（0x24000000 起）走默认内存映射；`.app_axi_ram` 只由单核 CPU 读写栈，没有 DMA 与 Cache 一致性问题，没有为这个段新增或修改 MPU 区域（与卷任务、Flash 启动任务、后台任务同一结论）。栈 4 KiB 是初值，由 R4.11 修正：Storage 单任务做全部工作时板测最低剩余 1480 B / 4096 B；读取任务的函数栈帧都很小（Debug 最大的 `audio_reader_end_stream_card_removed` 144 B，含 96 B 日志缓冲），最深的调用链（开流 → 按定位符打开 → 探测）与拆分前相同，所以预期余量不比 1480 B 差。

**启动**（`app_tasks.c` 的 `create_task`，即 Boot 任务）：Log Task → 可选的 SDRAM 自检 → `catalog_boot_init()` → **`audio_reader_boot_init()`（块池和两个信箱）** → `ui_cell_boot_init()`、`app_table_boot_init()`（#218，界面数据锁与各应用的内存准备）→ **Audio Read Task** → Volume Task → Playback Task → 前台任务（#218）→ Monitor、GUI、后台。块池 NOLOAD 段此后才有主（SDRAM 自检是破坏性的，必须在它之前独占运行，HWD-2），做完才创建用到块池和信箱的任务。读取任务入口：记下自己的句柄、登记卷事件订阅、（钩子打开时投一行 `HOOK:`）、进循环；第一圈**不等**，先比对一次卷序号、看一次请求信箱，所以它比第一条请求启动得晚、比卷任务启动得早或晚，都不会漏。

### Boot 栈核算（当时 128 word = 512 B；#220 起 192 word，最新核算见 [卷任务](../volume/README.md)“默认构建的 Boot 栈核算”）

块池和信箱初始化搬进 Boot 后新增的最深一条调用链（`-fstack-usage` 用工程的真实编译命令，Debug 与 Release 各量一次；`memcpy` 从 ELF 反汇编数 `push {r4, lr}` = 8 B）：

| 函数 | Debug | Release |
|---|---|---|
| `create_task` | 48 B | 40 B |
| `audio_reader_boot_init` | 8 B | 8 B |
| `audio_reader_block_init`（32 次 `xQueueSend`） | 24 B | 40 B |
| `xQueueGenericSend` | 48 B | 48 B |
| `prvCopyDataToQueue` | 32 B | 24 B |
| `memcpy` | 8 B | 8 B |
| **合计** | **168 B** | **168 B** |
| 一次任务切换最坏要在任务栈上留的（Cortex-M7 带 FPU，见 [卷任务](../volume/README.md)“Boot 栈核算”） | 约 204 B | 约 204 B |
| **需要** | **约 372 B** | **约 372 B** |

小于 512 B，余量约 140 B。`audio_reader_stream_boot_init`（两次 `xQueueCreateStatic`，`xQueueGenericCreateStatic` 40 B）和 `audio_reader_task_create`（24 B 加 `xTaskCreateStatic` 48 B）都比这条浅。Boot 里已有的最深链（可选的 SDRAM 自检加 `snprintf`）仍由 `app_tasks.c` 的编译期断言挡住，本票没有改变它。Boot 栈余量没有运行时读数（Boot 自删，Monitor 读不到它），这是静态核算，不是实测。

## 一个等待点与每圈的顺序

叫醒读取任务的只有三样：**新请求**（播放任务写请求信箱）、**块归还**（播放任务放回块）、**卷事件**（卷任务发布卷就绪或卷消失）。三者都是同一个通知槽 `AUDIO_READER_NOTIFY_EVENT_INDEX` 里的位，取走前一直保留。**不管哪个位叫醒的，每圈都按固定顺序做完**：

1. **卷事件**（`audio_reader_stream_apply_volume()`）：比对 SD 卷序号；变了就按“旧的全部作废，再看现状”处理，当前流仍被播放任务持有就写 `CARD_REMOVED`；
2. **请求信箱**（`audio_reader_stream_process_request()`）：有新请求就关旧流、开新流并应答；到点的 `SERVICE_BUSY` 开流重试也在这里重跑；
3. **读一块**（`audio_reader_stream_read_block()`）：最多一块；先处理在等的卷宽限（等到卷事件、超时，见下“失败约定”）；
4. **决定等多久**（`audio_reader_wait_ticks()`，纯函数）：

| 当前情况（按优先级从高到低） | 等多久 |
|---|---|
| 请求信箱里已有新请求 | 不等 |
| `SERVICE_BUSY` 重试在等 | 只等到重试时刻（约 10 ms），到了不等 |
| 卷宽限在等（卷不可用而卷没变） | 只等到宽限结束（初值约 100 ms），到了不等，让这一圈判超时 |
| 没有打开的流，或没有空闲块 | 一直等（暂停时块池满了就不再读，流保持打开） |
| 就绪块不到安全水位（4 块） | 不等，连续补块 |
| 够水位且还有空闲块 | 1 tick（让路） |

**进入循环时先看一次**：任务入口之后的第一圈等待 0 tick，直接走一遍 1、2、3，不依赖通知。播放任务在读取任务绑定句柄之前写的请求没有通知（句柄为空时通知被跳过），靠这一圈补上；启动顺序也让读取任务早于播放任务创建，这是两层保险。

**让路用等待点，不用 `vTaskDelay`**：水位以上每读一块等 1 tick，这 1 tick 就是通知等待的超时，让路期间新请求、块归还、卷事件能立刻叫醒。让的是 SD 锁：读取任务（3）放锁后马上又能拿到，优先级 2 的前台（以及优先级 1 的后台任务）排不上，所以必须真的睡一下。水位按就绪队列里的块数算，不含播放任务手里那块。水位 4 块（`AUDIO_READER_WATERMARK_BLOCKS`）、让路 1 tick（`AUDIO_READER_YIELD_TICKS`）、重试间隔 10 ms（`AUDIO_READER_BUSY_RETRY_MS`）、卷宽限 100 ms（`AUDIO_READER_VOLUME_GRACE_MS`）都是配置常量，标“初值，由板级验收修正”；让路改 0 即可做对比（R5.4），不算钩子。320 kbps 下 4 块约 3.3 s，够撑过最坏的非故障等锁 2.05 s（目前按 MP3 算，加高码率格式要重新核算）。切歌后补满 32 块的推算约 140 ms，前台最多等 14 至 27 ms（推算，未测）。

## 和别的任务怎么交错

- **和卷任务（3）**：同级，`configUSE_TIME_SLICING` 为 1，时间片轮转。卡检测中断只叫醒卷任务，读取任务正在跑时卷任务要等下一个时间片或读取任务阻塞才能运行（≤ 1 ms；读取任务几乎每次读都会在 DMA 完成信号量上阻塞，所以通常立刻轮到）。卷任务 `RefreshSD` 要拿 SD 卷锁：读取任务正持锁时，卷任务等它那一次调用做完（正常 3 至 7 ms，故障路径约 0.6 s），同级所以没有优先级继承可言；放锁后读取任务的下一次文件调用在 Filesystem 入口**等闸**（卷任务在检测边沿关了闸），卷任务拿到锁。闸开之后读取任务拿到失败返回（卡已拔）或正常结果（卡还在），再看卷序号。
- **和前台（2，#218）**：前台读标签时在 Filesystem 入口等 SD 锁。读取任务持锁时前台等它那一次调用做完；放锁后读取任务立刻再拿，前台排不上，直到读取任务进等待点睡 1 tick。所以水位以下（刚切歌补块期，约 140 ms）前台的名字、窗口行读取会被挡到补块完成，水位以上每读一块让一次，前台一次标签读取（至多 4 次读）推算等 14 至 27 ms；这是第 5 次上板 R5.4 要量的数（翻页注入钩子在切歌后立刻翻页，读 `FSLOCK` 行的 `wait=` 最长值）。前台持锁时读取任务要锁，前台经优先级继承临时升到 3 做完那一次调用。
- **和后台任务（1）**：同样的互斥规则，读取任务（3）、前台（2）都比它高，后台任务只在目录读取（曲库扫描，每条目录项一次锁）时持 SD 锁；它被挡住的时间不影响用户可见的延迟，扫描只是慢一点（#220 把扫描从 Storage Task 搬进后台任务）。
- **和播放（4）**：播放抢占读取任务；播放不拿任何文件锁（调用者拒绝名单，入口断言停机），所以读取任务持锁时被播放抢占不会让播放等锁。播放归还块和写请求都只是不阻塞地置通知位。
- **和后台（1）**：后台任务现在只有默认关闭的 Flash 压力钩子，只拿 Flash 卷锁，与 SD 卷锁互不相干。读取任务（3）会在补块期占满 CPU 的空隙之外让出，前台、GUI、Log、Monitor（2）先拿剩下的，后台（1）和 Flash 启动（1）再拿它们睡着时的空隙（#218：GUI 由 1 升到 2）；补块期是短脉冲（约 140 ms），不是常态。
- **读取任务持 SD 锁时卷任务要刷新会怎样**：见上面“和卷任务”。关键是读取任务在 Service 调用里持锁，卷任务的 `RefreshSD` 排在它后面；真拔卡时那一次读取 +1 ms 左右就失败返回，持锁 2.0 至 4.2 ms（#191 实测），不会等 500 ms 超时。

## 失败约定：读块和开流遇到 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE`

沿用 #211 的判断，再加一道有上界的卷宽限。**这和票面“回到等待点等卷事件”字面上不一样，原因如下**。

读块得到这三种返回值时，先对**同一个句柄 `CloseFile`**（结果不看）。它在 Filesystem 入口等闸，所以卡刚拔、边沿已到而 30 ms 消抖或对账还没结束时（卷任务已经关了闸），会一直等到本轮对账结束才返回，不会把拔卡误报成读错误；闸开着时立刻返回。然后看 SD 卷（`audio_reader_classify_read()`）：

- **序号和已处理的不同，或卷已不挂**：卡没了或换了。不写 `READ_ERROR`、不重试，FIL 已关，流“停住”（`StreamOpen` 为假，等待点一直等），**回到等待点等卷事件**，序号变了由第 1 步写 `CARD_REMOVED`。这就是用户看到 “No SD card”、游标重置的路径。
- **序号没变且卷仍挂着**：有两种可能，分不出来。① **卡刚拔、边沿已到而卷任务还没轮到**：读取任务和卷任务同为优先级 3，卡检测中断（`volume_sd.c` 的回调，`portYIELD_FROM_ISR`）只在被叫醒的任务优先级**高于**当前任务时才切换，同级不会立刻抢占。读取任务正在 CPU 上收尾（DMA 出错后的返回、`CloseFile`）时边沿到了，卷任务要等下一个时间片，闸还没关、序号还没变，`CloseFile` 直接通过。还有一个更窄的点：卷任务卸载之后、发布 `gone` 之前，调用在入口先看到“卷没挂”就直接返回 `INVALID_HANDLE`，不经过闸。原来 Storage 是 2，会被卷任务立刻抢占，所以没有这条交错。② **卡还在，设备出了错**（例如标签读取传输出错使设备进 ERROR，FatFs 的下一次读得到 `NOT_READY`；`FR_NOT_READY` 也映射成 `NO_MEDIA`，所以 `NO_MEDIA` 不等于卡真的不在），没有边沿就没有卷事件，一直等会静音到 T_data（5 s）才显示 “Read timeout”。
  - 所以既不能立刻报 `READ_ERROR`（①会把拔卡误报成 “Read error”，之后也不再写 `CARD_REMOVED`），也不能让 `NO_MEDIA` 一律等卷事件（②会把 #211 修掉的“静音到 T_data”带回来）。
  - 规则：进**卷宽限**（`AUDIO_READER_READ_GRACE`），在**唯一的等待点**上等最多 `AUDIO_READER_VOLUME_GRACE_MS`（初值 100 ms，覆盖消抖 30 ms 加刷新与卸载，卷任务卸载完成在边沿后 +34 至 35 ms，标“初值，由板级验收修正”）。期间来了卷事件（序号变了或卷不挂），按拔卡处理，写 `CARD_REMOVED`；超时仍没变，才写 `READ_ERROR` 并记 `Audio stream read failed.`。
  - 等待期间新请求、块归还、卷事件照常叫醒，**不是死等**：新请求取代旧流，宽限让位（`AUDIO_READER_GRACE_SUPERSEDED`，处理新请求的那一步清掉宽限）；每圈开头仍先比对卷序号。宽限期间流已经没有打开的句柄，所以不会再读。
  - 纯函数（`Tests/audio_reader`，`audio_reader_result`、`audio_reader_pace`）：`audio_reader_classify_read()` 给出 `GRACE`；`audio_reader_grace_step(request_pending, volume_changed, remaining_ticks)` 的优先级是 新请求 > 卷事件 > 超时 > 继续等；`audio_reader_wait_ticks()` 在宽限期间返回剩余 tick；`audio_reader_ticks_until()` 处理 tick 回绕。
  - 代价：#211 的“卡在、设备出错、没有边沿”的情形读取错误晚 100 ms 出现，不再是立刻，仍远小于 T_data。

**开流和探测**用同一规则（`audio_reader_open_follow_up()`）：打开、取长度、读头尾、定位这些调用得到这三种返回值，而曲库代次非 0、卷挂着且序号没变，就不立刻应答 `NOT_READY`，进开流宽限；等到卷事件应答 `NOT_READY`（卡没了，播放侧 “SD card not ready”、游标重置），超时应答 `IO_ERROR`（卡还在、设备出错，播放侧停止 READ_ERROR、游标保留）。曲库没好（代次 0）仍然立刻应答 `NOT_READY`，卷已经变了也立刻应答 `NOT_READY`，没有变化。`SERVICE_BUSY` 优先于宽限（先重试）。其他错误（卡还在）立刻报 `READ_ERROR` 或 `IO_ERROR`，不重试。

**记录失败种类的方式**：开流、探测、读头尾三处都经同一个函数 `audio_reader_open_failure(attempt, status, open_failure)` 记下“文件槽用尽”和“卷不可用”，再分类；`attempt` 是开流调用链上的局部变量，不用全局变量带话。

**拔卡和切歌同时发生（已知取舍，不改代码）**：每圈先处理卷事件、再处理请求信箱。若卷序号在本圈开头还没变，而新请求已经在信箱里，旧流不会写 `CARD_REMOVED`（它被新请求取代），新流的开启因卷变了或曲库代次 0 应答 `NOT_READY`，屏幕显示 “SD card not ready”（游标重置、播放停止）而不是 “No SD card”。播放仍然停止，只是提示不同；用户同时拔卡和点歌的概率低，且这种提示更接近“现在没有可播的东西”，所以保持现状。R4.5 若出现这种提示，记次数即可。

## 拔卡收尾与 `CARD_REMOVED` 时延

卷任务卸载后更新卷序号并叫醒订阅者，不等读取任务。读取任务醒来发现序号变了，当前流仍被播放任务持有（`audio_reader_stream_is_held()`：已成功开启、没有以读错误或拔卡终止；**已读完（ENDED）的流也算**，所以文件读完而播放还在放内存数据时拔卡同样立即停止）就写 `{当前流序号, CARD_REMOVED}` 并唤醒播放任务。卸载已让令牌失效，之后 `CloseFile` 只返回 `INVALID_HANDLE`，无害；“先写 `CARD_REMOVED` 再卸载”的顺序不再保证。换卡序号跳 2 同样处理。消抖由文件接口挡住（读取任务的调用只是多等约 30 ms），读取任务不用加消抖代码，满足“暂缓、不应答、流保持打开”。

写完之后记一行常开日志（INFO，标签 `Audio`，串口里标签后有冒号），**时延已在固件里算好**，相对卡检测的第一个边沿（R4.10）：

```text
I (12345) Audio: CARD_REMOVED lat=36ms seq=5->6 stream=17
```

`lat` = 写 `CARD_REMOVED` 的 tick − 卷任务记下的这次对账第一个边沿的 tick（1 tick = 1 ms，分辨率 1 ms，边沿时刻是卷任务发现它的时刻，比真实中断最多晚一个时间片）；`seq` 是读取任务已处理的卷序号和现在的；`stream` 是被终止的流序号。读不到边沿（例如开机挂载没有边沿）时写 `lat=-`。边沿时刻是这张票给卷事件加的诊断（`volume_event_note_edge()` / `volume_event_published_edge()`，APP 内部）：卷任务在每次对账的第一个边沿记下，随这次对账的卷事件一起发布，读的人读两次状态字夹着边沿，两次不同就放弃，不会拿到下一个事件的边沿。参考：卷任务的卸载完成在边沿后 +34 至 35 ms（#195 的修复版 spike），读取任务额外的延迟没有实测，R4.10 只记录。

## `SERVICE_BUSY`：文件槽用尽时重试

SD 只有 4 个文件槽、2 个目录槽（`_FS_LOCK` 9，Flash 2 加 1，共 9），槽用尽时打开返回 `SERVICE_BUSY`。它不是文件错误，也不是坏文件（原来的映射会把它归成 `BAD_FILE`，好歌被跳过，#194 第 13 条）：

- **开流**：打开或探测遇到 `SERVICE_BUSY`，关掉已打开的文件，**不应答**，保留请求，过约 10 ms 重新跑整个开流；等待用唯一的等待点（`RetryRemainingTicks`），期间新请求、卷事件仍能立刻叫醒；新请求取代旧重试；播放侧 T_data（5 s）兜底，到期它写“无流”请求，重试随之取消。
- **读块**：`SERVICE_BUSY` 放回这个块，约 10 ms 后在同一个文件位置重读（读取不会返回它，保险用）。
- **日志不刷屏**：同一次开流或读块遇到 `SERVICE_BUSY`，第 1 次记一行 WARN，之后每满 100 次（约 1 s）再记一行，5 s 内最多 6 行：`W (…) Audio: SERVICE_BUSY op=open pos=12 n=1`，不含文件名。R4.12、R6.10 搜 `SERVICE_BUSY`。

**R4.12：这张票上板时音频流和 Storage 会不会同时把 SD 的 4 个文件槽用尽？** 按代码核对，不会（前台一次切后的最新核算在文末加了一段，名称槽与窗口槽已被前台任务取代，同一时刻仍只开一个文件）。同时持有的文件：音频流 1 个（开流到读完或关闭，探测也在同一个句柄上）；Storage Task 是单线程，名称槽一次开 1 个文件、窗口槽每圈一行也是开 1 个文件、用完就关，两者不会同时；曲库扫描开的是**目录**（用目录槽，扫描由 Storage 同步做，期间名称槽和窗口槽不服务；#220 起扫描是后台任务的单步作业），且不开文件。所以 SD 上最多同时 2 个文件，加扫描时 1 个文件加 2 个目录，都在 4 + 2 以内。**#218 重算**：前台任务取代了 Storage 的两个槽，前台一步最多开 1 个文件（读完即关，不跨步持有），所以 SD 上最多同时 2 个文件（音频流 1、前台 1），曲库扫描仍开目录、不开文件（Storage 现在只剩扫描，仍是 1 个任务）；加扫描时 2 个文件加 2 个目录，仍在 4 + 2 以内，不会 `SERVICE_BUSY`。**#220 重核**：Storage Task 删除、扫描由后台任务做，扫描仍只开目录（一次最多 1 个目录句柄，目录槽 2 个），不开文件；回收和压力钩子只碰 Flash 卷（Flash 2 个文件槽、1 个目录槽，后台任务同时开的文件最多 1 个：压力钩子的压力文件），所以 SD 上最多同时 2 个文件（音频流 1、前台 1），不会 `SERVICE_BUSY`，R6.10 核对日志里没有它。每个任务同时持有的文件数的约定见 [前台任务](../foreground/README.md)“一步最多一次文件调用”，写进 inbox。

## `RefreshSD` 与 `UnmountSD` 之间的空隙

#211 留下的问题：卷任务的刷新和卸载是两个独立的持锁入口，放锁到再拿锁之间，已经过闸、正在等 SD 锁的调用可能抢到锁，读到新卡上按旧文件系统解释的数据。读取任务升到 3 之后与卷任务同级，空隙理论上可能被插进去。评估（读 `filesystem_entry.c`、`filesystem_lock.c`、`volume_sd.c`，不是实测，主机测试证明不了）：

**能不能发生**：要让读取任务（或别的调用者）在空隙里拿到锁，它必须在 `RefreshSD` 持锁期间还在等 SD 锁。闸在检测边沿就关了，之后新的调用都在拿锁之前等闸，不会来排队；所以只剩两种来源：① 过闸之后、拿锁之前被挂起超过整个静默期（30 ms 加刷新）的调用，两条语句之间只有断言和一次标志读取，能把它挂起这么久的只有播放任务（4）的一次很长的唤醒或中断风暴，现实不成立；② 早在边沿之前就排在锁上的等待者（例如 Storage 持锁做长调用时读取任务排队）：互斥量等待表同优先级先到先得，它排在卷任务前面，会在 `RefreshSD` **之前**拿锁，不在空隙里（它读到的是还没刷新的设备状态，属于“已持锁的调用不打断”的同一类）。即使有等待者，卷任务从刷新返回到调用卸载中间不阻塞，放锁只是把等待者放进就绪表，不交出锁，卷任务马上再拿；同级等待者只能靠时间片中断恰好落在这几微秒里，概率约 空隙长度 / 1 ms。

**后果（假设真的发生）**：读取任务读 1 块（≤ 32 KiB）新卡上按旧簇号算出的扇区，字节是垃圾，盖着旧流序号进就绪队列；读取任务只读不写，**不会写卡，不会数据错乱到卡上，不会死锁**（它放锁后下一次调用就在闸前等，卷任务拿锁卸载）。随后卸载让令牌失效、序号变化、读取任务写 `CARD_REMOVED`，播放停止，垃圾块随旧流作废；最坏听到几毫秒噪声（解码器遇到不成帧的字节会丢弃）。若新卡比旧卡小，旧簇号可能越界，读失败使设备进 ERROR，之后挂载失败、新卡要重新插拔才能恢复（卷任务“卡在但设备一直 ERROR、又没有边沿时不自动恢复”）。这些都需要上面不现实的前提。

**结论**：不会导致数据错乱或卡死，不必停下。要彻底关掉需要 Service 提供“刷新加卸载”的合并入口（一次持锁内做完刷新、对账所需的判断和卸载，或把对账判断交给 Service），那是公开接口变化，超出 spec M 节的授权，我没有加。选项：

- **A 现状**：接受，靠上面的分析和第 3、4 次上板的 R3.3、R4.5 拔插观察；成本 0。
- **B Service 合并入口**（用户批准后另开票）：`Service_Filesystem_ReconcileSD(...)` 在同一次持锁里刷新、判断、卸载，空隙彻底消失；需要把 `volume_reconcile` 的表搬进或回调进 Service，改接口。
- **C APP 侧缓解**：读取任务在每块读完发布前再比一次卷序号，序号变了就丢弃这一块（只挡“发布前序号已变”的读，挡不住空隙里的读，因为序号要卸载后才变）；收益有限，不建议。

## 块池内存与 HWD-2 审查（从 Storage README 搬来）

块池 32 × 32 KiB = 1 MiB 放 SDRAM NOLOAD 段，块内容入队才有效，不依赖清零，**块只由 CPU 读写，不是 DMA 目标**（SD DMA → bounce buffer → memcpy 进块 → 播放任务 memcpy 进窗口）。

- **启动**：`audio_reader_block_init()` 现在由 Boot 任务调用（原来在 Storage Task 里），晚于 Boot 任务里可选的 `storage_sdram_benchmark_run()` 全容量诊断（诊断是破坏性的，只能在块池入队前独占运行），早于创建音频读取任务和播放任务；只建队列并入队块指针，不触碰块内容。SDRAM 在 `main()` 经 `Platform/sdram` 完成正式初始化后任务才运行，满足“正式初始化之后、首次使用前”。队列控制块与存储放 DTCM `.bss`（块池两个队列 416 B，两个信箱 200 B，任务 TCB 120 B，读取任务的其余静态状态约 500 B，其中一次读全的曲目 280 B、探测缓冲 170 B）。
- **MPU**：`Core/Src/main.c` 的 Region 2 把 0xC0000000 起 32 MB 设为 Cacheable、Bufferable、不可执行、全访问，块池落其中；块只由单核 CPU 两个任务读写，无 DMA 与 Cache 一致性问题，ARC-18 不适用。
- **链接段**：块池在链接脚本 `STM32H743xG_flash.ld` 的独立段 `.storage_audio_pool`（NOLOAD，>SDRAM），位于 `.sdram_framebuffer` 之后；段名由 `audio_reader_config.h` 的 `AUDIO_READER_POOL_SECTION` 持有，**改名不改段名**（#202 A 节），链接脚本这次一个字没改。Debug map：`c05dca20`、`0x100100` B。`hw:pending`：上板确认 map 中块池落点、SDRAM 总用量在容量内。

## 与 Flash 回收（STOR-12）

打开的音频流不再暂停 Storage 的 Flash 回收：原来 Storage 在有打开的流时不提供回收机会，是因为回收和读块在同一个任务里，一次真擦除（约 40 至 50 ms，最长一次不可抢占）会挡补块。现在读块在读取任务、回收在后台任务的回收作业（#220，原来在 Storage），SD 与 Flash 各一把卷锁，擦除不再挡补块；#194 也写明“`audio_reader` 不再为回收提供‘流是否打开’的查询，除非 STOR-12 保留”，STOR-12 的现行文字是“打开的音频流不影响回收机会”，所以这次把条件去掉了。后果：播放中 Storage 每 100 ms 调一次 `ReclaimFlash`（空闲块高于水位时约 14 µs 就返回，不擦除；低于水位才真擦除），所以**播放中第一次可能出现真擦除**；STOR-12 的前提“运行中没有任务读 QSPI 映射窗口”以及播放中真擦除不欠载，要靠 R6.2 验证（第 6 次上板，用 Flash 压力钩子）。Flash 压力钩子自己每写一块也调一次回收（`BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM`），现在和 Storage 的 100 ms 回收叠加，擦除节拍比产品高一倍，#220 把回收做成后台作业时把它改成了 0（现在钩子默认不另调回收）。回收在 Flash 卷锁上等钩子的写时（最长约 1.5 至 3 s）会挡住 Storage 的名称和窗口读取，这只在钩子构建里出现。

## 曲目诊断计数器的并发

`catalog_track_open()` 现在被两个任务调用（读取任务开流，前台任务读曲名和窗口行，#218；原来是 Storage Task 的名称槽、窗口槽），其中的 `catalog_track_diag_locator_miss()` 对计数器做读-改-写。逐个核对 `catalog_track_diag.c` 的静态变量：**只有定位符核对不符计数器被两个任务同时更新**，其余计数器（`Reads`、`ByReason`、`MaxUs`……和窗口累加）只由曲名与窗口行所在的任务（前台任务）写；计时函数和 `Stream` 行没有状态。处理：把 `LocatorMisses` 从计数器结构里**分开**成独立的 `volatile` 变量，自增放在 `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()` 之间（两条指令的短临界区），日志在临界区之外投。主机测试是单线程的，这条只能靠读代码和 Release 反汇编；不加测试专用的访问函数。前台一次切之后窗口累加由前台任务写，仍然只有一个写者。

## 日志与消息池（DIAG-3）

串口里每行前缀是 `<时间> <I|W|E> (<tick>) <标签>: `，标签后面有冒号。本任务的行：

| 行（标签 `Audio`） | 级别 | 时机 | 节拍 |
|---|---|---|---|
| `Audio read interrupted: volume unavailable, waiting for volume event.` | INFO | 读块遇到卷不可用且卷变了，流停住 | 每个被打断的流最多 1 行 |
| `CARD_REMOVED lat=<ms>ms seq=<前>-><后> stream=<流序号>` | INFO | 写 `CARD_REMOVED` 之后（常开，R4.10） | 每次拔卡每个被持有的流 1 行 |
| `SERVICE_BUSY op=<open\|read> pos=<位置> n=<第几次>` | WARN | 文件槽用尽重试 | 第 1 次和每满 100 次 |
| `Audio stream read failed.` | ERROR | 读块得到文件错误 | 每个流最多 1 行 |
| `Volume event subscription failed.` | ERROR | 订阅表满（不会发生） | 开机 1 次 |
| `HOOK: locator_miss ON` | WARN | 钩子打开时任务入口 | 开机 1 次 |

`Stream pos= off= open= lseek=`（INFO）与 `Locator miss pos=`（WARN）仍由曲库的诊断模块投，标签 `STORAGE`，格式不变。

**消息池核算**（Service Log 30 块，Log Task 每 5 ms 转交一条，排空约 5 ms/行）：Monitor 稳态现在 10 个任务共 15 行（`Audio Read Task` 多一行），开机后 `MountFlash` 期间 11 个任务共 16 行；最坏同时到达 = Monitor 16 + Filesystem 持锁统计 3 + 曲库短锁 1 + 一次拔卡 6 = **26 行，留 4 块**。拔卡的 6 行沿用上一票的口径（卷路径日志按上界 5 行：`sd edge`、`sd settle`、人读日志，刷新失败时再加诊断），加本任务新增的 `CARD_REMOVED lat=` 1 行；上一票的口径没有计入读取被打断时的 `Audio read interrupted` 和播放侧的 `Stop reason=CARD_REMOVED`（各 1 行，一次正常的播放中拔卡实际是 `sd edge`、`sd settle`、`Card removed.`、`Audio read interrupted`、`Stop reason`、`CARD_REMOVED lat` 共 6 行，刷新失败的罕见路径才到 8 行），更保守的口径是 Monitor 16 + 3 + 1 + 8 = 28 行，留 2 块。`SERVICE_BUSY` 是异常路径，同一时刻最多再加 1 行（26 行变 27 行，留 3 块；保守口径 29 行，留 1 块）。Flash 压力钩子打开的构建再加统计行 1 行；它的“卷满”和“停下”两条偶发行同时撞上整套最坏情形是 30 行以上，没有余量，实际上这几条同时到达的概率很低，真有丢失会在 Monitor 的 `Health log_svc=` 里数出来，丢了要注明、受影响的行不算通过（R1.3）。定位符失效钩子打开的构建每次开流多一行 `Locator miss`（WARN），只在用户点播放时出现，不与 Monitor 的批次叠加成新的最坏情形。

**#218 重算（用户 2026-11-06 的决定，池加大）**：保守口径下最坏同时到达从 29 行（含 `SERVICE_BUSY`）变成 34 行：Monitor 17 行（开机后 `MountFlash` 期间 12 个任务加 5 行固定行）+ Filesystem 持锁统计 3 + 曲库短锁 1 + `ui_cell` 持锁统计 1 + 显示延迟汇总 1 + 前台 step 耗时 1 + 拔卡 8 + `SERVICE_BUSY` 2（本任务与前台任务各 1 行，文件槽用尽时会同时到）。池从 30 块加大到 **45 块**（`LOG_SERVICE_QUEUE_LENGTH`，多 2 KiB DTCM），**留 11 块**（Flash 压力钩子构建 37 行留 8 块，翻页注入钩子构建 35 行留 10 块），`monitor_task.c` 的 `_Static_assert` 核对 `12 + 5 + 17 + 8 = 42 ≤ 45`。明细见 [前台任务](../foreground/README.md)“日志与消息池核算”。上一票的数字（26 行留 4 块，保守口径 28 行留 2 块）作废。

**Monitor 登记**：`monitor_task.c` 的任务登记表加了 `Audio Read Task`（栈 1024 word），漏了会出 `Task not registered in monitor` WARN。任务名不能叫 `Audio Reader Task`（17 个字符，`configMAX_TASK_NAME_LEN` 16 会截断，登记对不上）。CPU 行的短键是 `audi`，与已有的 `crea`、`log`、`stor`、`volu`、`flas`、`moni`、`gui`、`play`、`idle`、`tmr`、`back` 互不相同。可能同时出现在一批里的任务是 11 个（`Create Task` 创建完就自删，早于 Monitor 的第一批，不会和 `Flash Boot Task` 同批），最坏（10 个各占 10.0 %）仍是 120 字符，不超过 127，`Tests/monitor_warn` 核对；如果 `Create Task` 将来不自删，12 个任务各占两位数会到 129 字符，要缩键或拆行。**#218 加前台任务后，同批任务数是 12 个，这个情形成了现实：`monitor_cpu_format()` 在放不下时整行退化成整数百分比，保证 127 字符以内**（见 [Monitor](../monitor/README.md)）。

## 定位符失效测试钩子（默认关闭，DIAG-7）

用于 R4.4：定位符核对失败时，回退按路径打开最后一首，T_data 内要能出声。写法照前几个钩子：`audio_reader_config.h` 里 `AUDIO_READER_HOOK_LOCATOR_MISS`（默认 `0U`），代码是 `if (AUDIO_READER_HOOK_LOCATOR_MISS != 0U)`，默认构建也编译；根 `CMakeLists.txt` 的缓存变量 `MP3_HOOK_LOCATOR_MISS`，**只允许 Debug**（非 Debug 构建开会在 configure 时报错）；启动日志一行 WARN `HOOK: locator_miss ON`（串口里标签后有冒号，搜 `HOOK: locator_miss ON`）。

做法：每次开流和重开流前，读取任务把曲库读出的文件定位符清零（全零是“无定位符”），Filesystem 一定判核对不符，走真实的“退回按路径打开”路径，并带出 `Locator miss pos=` WARN 和计数器；音频流照常用它。#218 起前台任务读曲名和窗口行前也走同一个开关（`music_foreground_apply_locator_hook`），所以按路径打开的是整条链路（R5.5：切歌不显示 `Read timeout`，前台回退按路径打开不挡音频流）。大目录卡（`--set bigdir`，12000 首同文件夹）上开最后一首，按路径打开 1.43 至 2.05 s，期间读取任务自己持 SD 锁，前台的调用等它；T_data 5 s 之内应能出声，屏幕不显示 “Read timeout”。

开：`cmake --preset firmware-debug -DMP3_HOOK_LOCATOR_MISS=1`；关：`-DMP3_HOOK_LOCATOR_MISS=0`（缓存变量会留在构建目录里，开过要显式关）。

## 主机测试证明什么、不证明什么（HAR-5）

`Tests/audio_reader`（3 个 CTest：`audio_reader_range_locate` 越过标签与跳转偏移、`audio_reader_result` 失败结果加卷序号的分类和日志格式、`audio_reader_pace` 决定等多久）证明：每种失败结果在卷变了和没变时写什么（卷没变进宽限）、卷宽限里每圈的判断（等到卷事件、超时、等待中来了新请求，优先级 新请求 > 卷事件 > 超时 > 继续等）、开流遇到卷不可用而卷没变时进宽限（等到事件应答 `NOT_READY`，超时应答 `IO_ERROR`）、哪些流在卷序号变了时写 `CARD_REMOVED`（含读尽的流、序号回绕）、`SERVICE_BUSY` 的重试和日志节拍（5 s 内 6 行）、水位和让路和一直等和重试时刻和宽限剩余时间的优先关系、距截止时刻的剩余 tick（含 32 位回绕）、两行日志的格式和长度不超过 127 字符。期望值来自 #194、#195 和 #202，不用代码里的表达式重算。主机测试是单线程的，**不证明**：真实任务抢占与优先级继承、通知的真实唤醒时序、读取任务与卷任务同级时间片的真实交错、`CloseFile` 在闸前等多久、`SERVICE_BUSY` 重试的真实时刻、块池和信箱、`audio_reader_stream.c` 里的调用顺序（读失败后先 `CloseFile` 再取卷状态、宽限开始与结束时写什么）、宽限的 100 ms 盖不盖得住真实的消抖加对账、栈余量、水位 4 块和让路 1 tick 在真实负载下够不够——这些靠第 4 次上板（R4.1 至 R4.12 和回归行）。读取任务没有主机测试程序，`for(;;)` 与 FatFs 调用不做主机测试。

## 约束

- 读取任务不解码、不碰播放状态机；播放任务不碰文件、不拿文件锁。
- 读取任务绝不同时持两把卷锁，不在持文件锁时调用会阻塞的接口（`Service_Log_Post` 不阻塞，可以调用）；它不在调用者拒绝名单里。
- 订阅者上限 `VOLUME_SUBSCRIBER_MAX` 为 4：#220 删掉 Storage Task 后，音频读取任务、前台任务（#218）和后台任务占 3，余 1 个。
- 命名：任务内部使用 `audio_reader_*`；跨层调用使用 `Service_Filesystem_*` 与 `Service_Log_*`；不再有 `Storage_AudioMediaTypeDef`。
