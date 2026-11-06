# 音乐曲库（`APP/apps/music/catalog/`）

音乐应用的曲库：SD `Music/` 的路径事实表（SDRAM `.storage_catalog` 字符串池 + 偏移/长度 + 文件定位符，不存卷字段）、顺序播放列表，以及曲目显示名、曲目标签文本、曲目打开与相关诊断的纯函数和封装。0.10.0 从 `APP/tasks/storage/catalog/` 搬来（[#212](https://github.com/immorcoding/mp3_project/issues/212)，依据 [#189](https://github.com/immorcoding/mp3_project/issues/189)、[#196](https://github.com/immorcoding/mp3_project/issues/196)，spec [#202](https://github.com/immorcoding/mp3_project/issues/202) F 节）。规则见 STOR-1、STOR-7、STOR-17、ARC-19，技术事实见 [storage.catalog.md](../../../../docs/shape/storage.catalog.md)。

**搬家那张票只搬家和加规则**：扫描这时仍由 Storage Task 同步调用（`catalog_init()`），改成单步函数留给后台任务票。Queue 窗口槽和曲目显示名槽已在前台一次切（[#218](https://github.com/immorcoding/mp3_project/issues/218)）删除，曲名与窗口行改由前台任务经 `ui_cell` 的格子发布（见 [音乐前台流程](../foreground/music_foreground.h) 与 [前台任务](../../../tasks/foreground/README.md)），读者仍是这里的 `catalog_read_track()`。**发布成功后叫醒前台任务一次**（`foreground_task_wake()`，APP 内部接口）：GUI 不会重写没变的需求，前台要主动重新处理最新需求（曲名答过代次 0 的、窗口格里还是空窗的）；被丢弃或失败的扫描不发布，也不叫醒。链接段名 `.storage_catalog`、`.music_sheet` 不改。

## 文件

| 文件 | 内容 | 主机测试 |
|---|---|---|
| `catalog.c/.h` | 扫描、发布、作废、读者一次读全、曲库短锁与持锁统计 | 是（`Tests/music_catalog`，假 Filesystem、假卷状态、共用 FreeRTOS 桩） |
| `catalog_sheet.c/.h` | 顺序播放列表（`SeqList[i] = i`，SDRAM `.music_sheet`），只由 `catalog.c` 用，没有自己的锁和代次 | 经 `catalog.c` |
| `catalog_config.h` | 容量、扫描重试间隔、持锁统计开关与节拍、诊断阈值与缓冲 | 编译期断言 |
| `catalog_name.c/.h` | 曲目显示名纯函数 | 是 |
| `catalog_text.c/.h` | 曲目文本纯函数（ID3 标签） | 是 |
| `catalog_track_open.c/.h` | 曲目文件的统一打开入口（定位符，核对不符退回路径） | 是 |
| `catalog_track_text.c/.h` | 曲目文本的文件读取封装 | 是 |
| `catalog_track_diag.c/.h` | 标签读取与打开诊断 | 是 |

## 发布规则（STOR-1、#196）

一份表，模块内一把短锁；**持锁只拷内存**，不调文件接口、不投日志、不等别的对象，所以曲库锁和文件锁永远不嵌套。

- **已发布的状态只有三个字**：代次 `Generation`、条数 `Count`、发布时的卷序号 `VolumeSequence`，都在 `.bss`，只在持锁时读写。表本身（路径池、条目、定位符、播放列表）扫描时由扫描者**不拿锁**地写。
- **开始扫描**（`catalog_music_init()` 开头）：锁内把代次和条数置 0，记下此刻的 SD 卷序号（`volume_status()`，一个字，不拿锁）。此后读者看到代次 0，**不碰表**。
- **发布**：播放列表建好以后，锁内比对卷序号：卷挂着且序号与开始时一致，才写代次和条数；不一致则不发布（曲库保持代次 0，结束行 `res=aborted n=0`）。
- **代次只在发布时加 1**（跳过 0），计数在 `.bss`，一次开机内不重复；`catalog_invalidate()` 只把代次和条数置 0，不动计数。代次 0 永远表示“没有可用曲库”，所以播放列表自己的 `Generation` 字段取消了。
- **取代次时卷序号不符就算 0**：`catalog_generation()` 和 `catalog_read_track()` 在锁内用 `volume_status(VOLUME_ID_SD)` 比对，卷不在或序号不符都得 0，不需要任何任务调用作废。`catalog_invalidate()` 因此不是正确性必需，只是让状态尽快归零。
- **读者一次读全**：`catalog_read_track(position, &out)` 在同一把锁内完成“播放列表位置 → 曲库下标”，并拷出代次、条数、路径和文件定位符（`Catalog_TrackTypeDef`，约 300 B，调用者放静态存储）。代次为 0 时条数为 0、路径空串、定位符全零，表一个字节都不碰；位置越界时代次和条数有效、路径空串，返回 `false`。取代了原来分开取代次、条数、路径、定位符（四次之间可能被重扫插进来）的写法。

读者现在有三处，都是一次读全：音频流开流（音频读取任务的 `audio_reader_open()`，经 `catalog_track_open()` 用同一次读到的定位符）、前台任务的曲名需求与 Queue 窗口的每一行（`music_foreground.c`，每步一行）。前台发布的曲名结果与窗口的代次取自这一次读全，不再另取。

### 各情形

| 情形 | 行为 |
|---|---|
| 扫描中途卡被换 | 每读一条目录项前比对卷序号；变了就关目录（返回值不看）、丢弃本轮、不发布、不记 ERROR。卡还在的话，Storage Task 下一圈看到新的卷序号会重扫。文件接口在闸关着时返回 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 的，也按同样丢弃处理 |
| 扫描做完、发布之前换卡 | 发布时锁内比对卷序号，不一致不发布 |
| 读者在扫描期间读 | 看到代次 0，不碰表，也看不到上一份曲库的路径 |
| 发布和读者交错 | 发布和每次读都在同一把锁内：读者要么在发布之前（代次 0，不可用），要么在发布之后（完整的一份），读不到半份 |
| 代次 0 时读者的行为 | `catalog_read_track()` 返回假、`Generation` 为 0、`Count` 为 0；音频流开流答 `NOT_READY`，前台的曲名答代次 0 空名，窗口发布代次 0 的空窗 |
| 取代次时序号不符 | 算 0，同上；换卡后旧窗口、旧流在下一次读时就失效，不靠谁调用作废 |

### 锁的创建时机

锁是静态互斥量（不占堆），由 `catalog_boot_init()` 创建；它在 `APP/tasks/app_tasks.c` 的 Create Task（Boot）里、SDRAM 自检之后、创建 Storage Task 和音频读取任务**之前**调用一次，所以早于任何会读写曲库的任务的第一次使用（音频块池与信箱的 `audio_reader_boot_init()` 紧跟其后）。漏调用会在第一次拿锁时 `configASSERT` 停机。它同时复位已发布的状态和代次计数，整个开机只调一次，“一次开机内不重复”因此成立。以后创建音频读取、前台、后台任务的票沿用同一位置（或改为注册表的 `boot_init`）。

## 扫描与错误分类（#196，沿用 STOR-7）

| 结果 | 处理 | 实现 |
|---|---|---|
| `SERVICE_BUSY` | 过约 10 ms 重试，不算失败 | 打开、读、关目录各一处循环，`CATALOG_BUSY_RETRY_MS`；等的时候卷序号变了就作废这一轮 |
| `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` | 丢弃本次扫描，不发布，等卷事件 | `Service_Filesystem_StatusIsVolumeUnavailable()`，结束行 `res=aborted` |
| 打开根目录时的其他错误 | 按空库发布，记 WARN | `Music Directory Open Failed.` |
| 打开子目录时的其他错误 | 跳过这个子目录，记 WARN | `Sub Directory Open Failed.` |
| 读或关目录时的其他错误 | 不发布，记 ERROR，等下一次插卡 | `Music Directory Read Failed.`（读）与 `Music catalog dfs failed.`，结束行 `res=dfs_error` |
| 条目满、池满 | 发布截断结果 | `Music Catalog Full.` / `Music Catalog Pool Full.` WARN；池按 4 MiB 与 12000 条的平均 350 B 设计，路径不超过 255 B 时条数先到上限，池满只是防御，主机测试到不了 |

**打开目录得到卷不可用时再开一次**（#211）：在途调用被拔卡打断时，第一次失败发生在卷任务对账之前、卷序号还没变；路径打开没有句柄可借来等闸，所以再开一次，它在 Filesystem 入口等闸，等到对账结束才下结论——卡没了（再次得到卷不可用，丢弃）或卡还在（照常打开，扫描继续）。根目录和子目录都一样。

**扫描结束后的判决** `catalog_scan_verdict(volume_unchanged)`：`CATALOG_SCAN_DONE`（没有被丢弃，照常就绪）、`CATALOG_SCAN_EVENT_PENDING`（被丢弃且卷变了，等卷事件重扫、不置就绪）、`CATALOG_SCAN_UNREADABLE`（被丢弃但卷没变：卡还在、读不了，没有边沿就没有卷事件，Storage Task 记 ERROR、置就绪、曲库为空）。扫描中途或发布前卷序号变了而丢弃，同样走这个判决。

目前的 `SERVICE_BUSY` 重试用任务延时（扫描还是同步调用，会占着 Storage Task）；改成单步函数后由返回值告诉后台任务多久后再叫它，卷序号比对和错误表不变。

## 持锁统计（DIAG-6，与 Filesystem 持锁统计同一个开关）

`CATALOG_LOCK_STATS_ENABLE`（`catalog_config.h`）：没有 `NDEBUG` 的构建（固件 Debug）开，带 `-DNDEBUG` 的 Release 整段编译掉，不占时间也不占字段。只记开机以来的**最长持有**和拿取次数（周期计数器量，微秒；区间超过 4 s 或计数器未启动时改用 tick），不记逐次日志。

每 `CATALOG_LOCK_STATS_PERIOD_MS`（10 s）在某次读写曲库、**放锁之后**投一行 INFO，没有读写就没有汇总。标签 `CATLOCK`，串口输出里标签后有冒号：

```
CATLOCK: n=1204 hold=96us
```

`n` 是拿取次数，`hold` 是最长一次持有的微秒数（开机以来的最大值，不清零）。持锁的操作只有拷内存和一次 `volume_status()`，预期远小于 100 µs；板级验收（#202 的“曲库短锁最长持有”）读这一行。

### 日志消息池核算（DIAG-3，汇总类日志注明节拍）

本票（#212）新增 1 行（每 10 s 一次，Debug 才有，不超过 36 个字符），当时池 30 块、最坏同时到达 23 行。之后每加一个任务或一种常开日志行都重算过：#213 后台任务 24 行，#215 音频读取任务 26 行（保守口径 28 行，留 2 块），**#218 前台一次切加前台任务、前台 step 耗时、`ui_cell` 持锁统计和显示延迟汇总后保守口径是 33 行，池加大到 44 块，留 11 块**，最新数字与明细见 [前台任务](../../../tasks/foreground/README.md)“日志与消息池核算”和 [Monitor](../../../tasks/monitor/README.md)。Release 不投本行。

## 公开 Interface

- `catalog_boot_init()`：见上，Create Task 里调用一次。
- `catalog_init()` / `catalog_music_init()`：扫描并发布，返回 `Catalog_StatusTypeDef`（`CATALOG_OK`：扫描完成并发布，或本轮被丢弃；`CATALOG_ERROR`：读或关目录失败）。`catalog_books_init()` 是空桩，恒成功，当前不调用。仅 Storage Task 在卷就绪之后调用；扫描时不 memset 整池。
- `catalog_invalidate()`：拔卡或卸载时锁内把代次和条数置 0。
- `catalog_generation()`：当前代次，0 表示不可用。
- `catalog_read_track(position, &out)`：读者一次读全，见上。
- 容量（STOR-17）：`CATALOG_MUSIC_MAX_NUM` = 12000 首（保证可同在一个文件夹），路径字符串池 4 MiB。每条随附 16 B 的不透明文件定位符（ARC-19，`MusicCatalogLocator[12000]`，约 188 KiB，与偏移/长度表同在 `.storage_catalog` 段、同下标），只在同一曲库代次内有效；扫描时个别位置取不到定位符则为全零，按它打开会核对不符并退回路径打开。启动不把该段并入 `.bss` 清零。没有 `Music/` 时为空表成功。

## 从 Storage 搬来的接口说明

- `catalog_display_name()`：曲目显示名纯函数（`catalog_name.h`），由曲库 UTF-8 相对路径取最后一个 `/` 之后的文件主名并去掉末尾 `.mp3`（大小写同扫描），主名超过 251 B 或非法 UTF-8 返回 `false`，不截断。无 RTOS 依赖，主机测试见 `Tests/music_catalog`；由曲目文本函数在回退时调用。
- `catalog_track_text()`：曲目文本纯函数（`catalog_text.h`），输入曲库相对路径、文件长度、按偏移读文件回调、GBK 查表回调和 4 KiB 工作缓冲，输出曲目显示名（可用 TIT2，否则文件主名）、歌手（TPE1，取不到为空串）、结果分类（取到、无 ID3v2、帧缺失、解码失败、超读取上限、读取错误，以标题为准）和读取次数。首块 4 KiB、按需 seek、含首块最多 4 次读取（`CATALOG_TEXT_BLOCK_BYTES`、`CATALOG_TEXT_MAX_READS`，在 `catalog_config.h`，初值，由板测修正）、不越过标签总长；带标签级去同步的标签只解析首块。帧定位与文本解码在 `Components/id3_reader`。不访问 FatFs、RTOS 或日志，计时、日志、`f_open` 失败判定与格子发布由调用方做；曲名与窗口行都经 `catalog_track_text_read()` 接入。返回 `false` 只表示文件主名提取失败（STOR-15 的防御分支）。主机测试见 `Tests/music_catalog`。
- `catalog_track_text_read()`：曲目文本的文件读取封装（`catalog_track_text.h`），前台任务的曲名需求与窗口行共用这一份（同一 4 KiB 工作缓冲与结果，不另设第二份），只在前台任务调用。入参是播放列表位置（只用于核对不符的日志）与调用者先做的 `catalog_read_track()` 一次读全的结果（路径与定位符），本函数不再自己取路径；经 `catalog_track_open()` 打开 SD 文件（见下一条）、取长度，以“seek + 读”作为按偏移读回调、以 `Service_Filesystem_GbkToUnicode()` 作为 GBK 查表回调调用 `catalog_track_text()`，用完即关 FIL；与音频流 FIL 合计不超过 FatFs `_FS_LOCK`（2）。打开失败置 `OpenFailed`、取长度失败按“读取错误”，都回退文件主名、歌手为空；打开得到 `SERVICE_BUSY`（文件槽用尽，瞬时的）另置 `OpenBusy`，前台过约 10 ms 重试、不缓存（ARC-27）；结果的 `OpenUs` 是打开耗时（与标签读取耗时分开）。路径为空（位置无效）返回 `NULL`。返回本 Module 持有的结果（下一次调用前有效），`NULL` 只表示文件主名提取失败。4 KiB 工作缓冲与结果放 DTCM `.bss`（约 4.6 KiB），不占任务栈；一次读全的结果（约 300 B）由各调用者放自己的静态存储。**DTCM 放置的审查结论（HWD-2、HWD-6）**：缓冲只由 CPU 读写（id3_reader 解析、catalog_text 解码与清理），不是 DMA 缓冲；SD 读取经 Filesystem Service 的 AXI SRAM bounce buffer（32 KiB，`filesystem_config.h`）由 CPU 拷入，DMA 不直接访问 DTCM，也无需 Cache 维护（DTCM 不经 D-Cache）；放 DTCM 不免启动与 MPU 审查，两者不变（静态 `.bss`，由启动代码清零，无 NOLOAD 填充需求，不涉及 MPU 区域）；占用余量：本分支固件构建（`build-firmware.ps1`，debug）DTCMRAM 已用 109096 B / 128 KB（83.23%），剩余约 21.5 KiB，缓冲 4096 B + 结果 507 B 在其中，另无新增堆或任务栈。计时、计数与日志不在此，由两个槽的调用点经 `catalog_track_diag_*()` 做（见下一条）。
- `catalog_track_open()`：曲目文件的统一打开入口（`catalog_track_open.h`，STOR-17、ARC-19、DIAG-5），**曲名、Queue 窗口行（二者经 `catalog_track_text_read()`）、开启音频流与跳转重开流四条路径都经它打开**，由前台任务（曲名、窗口行，#218）和音频读取任务（开流、重开流，#215）调用，函数本身没有静态状态，可以被两个任务同时调用；唯一被它们同时累加的是 `catalog_track_diag_locator_miss()` 的核对不符计数器，在短临界区里加（见 [音频读取任务](../../../tasks/audio_reader/README.md)“曲目诊断计数器的并发”）。先用一次读全结果里的定位符调 `Service_Filesystem_OpenFileByLocator()`：`OPENED` 即成功；`MISMATCH`（无定位符、条目已变、是目录、起始簇或长度不同）退回 `Service_Filesystem_OpenFile()` 按路径打开，并记 `Locator miss pos=N` WARN、累加计数器；`FAILED`（槽用尽、卷未就绪、介质错误等）不退回，把 Service 状态原样交给调用者。输出的打开耗时（DWT，µs）含核对不符后退回路径打开的那一次，失败时同样给出。四条路径各自仍用原有的失败映射（名称槽/窗口行回退文件主名，开流按 `BAD_FILE`/`NOT_READY` 等）。主机测试见 `Tests/music_catalog`（`catalog_open`）。
- `catalog_track_diag_*()`：标签读取与打开诊断（`catalog_track_diag.h`，#141、#179，DIAG-4、DIAG-5），由前台任务的曲名与窗口行、`catalog_track_open()` 与音频流开启使用（后两者也在音频读取任务里调用），不改读取行为、不进 ISR 与每帧路径。依赖 Cortex-M7 周期计数器 Adapter（DWT，只 `Read()`，不复位）与 Log Service。**计时**：单行耗时 = 打开（按定位符，核对不符时含退回路径的那次）+ 读取 + 解析 + 关闭，换算为 µs；其中打开耗时另作 `OpenUs` 单独记录。**计数器**（前台任务私有，自启动累加，不对外公开；命中行缓存的行不读文件，不计）：读取次数（读回调调用次数合计，含首块与后续 seek 读，打开失败计 0；与日志里的 `reads=`、`rd=` 同一口径）、取到数、六类回退数（无 ID3v2 / 帧缺失 / 解码失败 / 超读取上限 / 打开失败 / 读取错误；各类之和即标签读取的文件数）、单行耗时最长值与累计值、单次打开耗时最长值、`Locator miss` 次数（板上预期为 0）。**窗口汇总阈值（STOR-17、DIAG-4，初值，由板测修正）**：单行打开 > `CATALOG_OPEN_BUDGET_US`（30000 µs）或整窗总耗时 > `CATALOG_WINDOW_BUDGET_US`（500000 µs），均为 `catalog_config.h` 的配置常量，`Tests/music_catalog` 的编译期断言核对其值。**单行日志阈值**：`CATALOG_TRACK_BUDGET_US`（`catalog_config.h`）= 1 s（1000000 µs，初值）。单行读取（含打开）超过它是窗口汇总的第三个触发条件。它原先等于 T_data/2 并有编译期断言钉住；它不再与播放任务的配置比较（T_data 会随音频读取票改动），只是一个日志阈值，`Tests/music_catalog` 保留它大于另外两个预算的两条编译期断言。**日志**（tag `STORAGE`，文本只含播放列表位置与原因名，不含文件名、标题或歌手）：
  - 曲名需求每次读文件（缓存未命中）一行 INFO：`Tag idx=N src=title reads=R open=O us=U`，或回退时 `Tag idx=N src=file why=<原因> reads=R open=O us=U`；`open` 是打开耗时，`us` 是标签读取耗时（整行耗时减去打开耗时），两者分开；`why` 取 `no_id3v2`、`frame_missing`、`decode_failed`、`read_limit`、`open_failed`、`read_error`。
  - 原因为 `decode_failed`、`read_limit`、`open_failed`、`read_error` 时另记一行 WARN：`Tag warn idx=N why=<原因>`。`no_id3v2` 与 `frame_missing` 是正常情形，不记 WARN。文件主名提取失败的防御分支沿用原有 `Name fail` WARN，不计数。
  - Queue 窗口逐行不记。窗口加载完成（打 `READY` 前）时，若本窗有上述四类 WARN 级原因、或本窗最长单次打开 > 30 ms、或整窗总耗时 > 0.5 s、或最长单行 > 1 s（`CATALOG_TRACK_BUDGET_US`），记一行汇总：`Tag win rows=行数 max=本窗最长单行（含打开） omax=本窗最长单次打开 top=自启动最长单行 us=总耗时（含打开） rd=读取次数合计 ok=取到数 nid3=无ID3v2 nofr=帧缺失 dec=解码失败 lim=超读取上限 open=打开失败 rerr=读取错误`（耗时单位 µs；耗时字段排在原因计数之前，万一撞上 Service Log 单条 128 B 上限被截断，丢的是尾部计数而不是 `max`/`top`；加入 `omax` 后十位数的极端取值会超出 128 B 并丢尾部计数，现实取值（耗时 < 10^7 µs）约 120 字符，不截断）；有 WARN 级原因为 WARN，否则 INFO。其余窗口只累加计数器。以空窗收尾的窗口不记汇总。
  - 开流和重开流成功后各记一行 INFO `Stream pos=N off=<B> open=<us> lseek=<us>`（`catalog_track_diag_stream_done()`，DIAG-5）：`pos` 是播放列表位置，`off` 取请求里的 `AudioOffset`（未钳位），`open` 是打开耗时，`lseek` 是定位到音频区间起点加偏移的最后一次 `f_lseek` 耗时；只在曲目或跳转边界记，不在每块读取路径上；开启失败不记（失败由播放侧的 `Open` 日志与现有错误路径呈现）。
  - 核对不符一行 WARN：`Locator miss pos=N`，文本只含位置，不含文件名。
  - **扫描结束一行 INFO**（DIAG-6、#204，常开，tag `Catalog`，不是 `STORAGE`）：`Scan done res=<结果> n=<条数> us=<微秒>`，每次 `catalog_music_init()` 结束时一行。`res` 取 `ok`（含没有 `Music/`、子目录打不开、条数满截断，此时 `n` 是已收录的条数）、`dfs_error`（目录读取或关闭失败，此前已有一条 ERROR）、`aborted`（本轮扫描被丢弃：卷不可用、扫描中或发布前卷序号变了，此时 `n=0`，不记 ERROR）。`us` 是从扫描开始（作废旧表之后）到遍历结束的耗时，含目录打开与读取以及期间被更高优先级任务抢占的时间，不含建播放列表和发布；DWT 计数器 32 位、480 MHz 下约 9 s 回绕，所以扫描里每次目录打开和读取之后把周期差累加进 64 位计数（相邻两次相隔一次目录操作），不依赖首尾两次读数。主机测试 `Tests/music_catalog` 的 `catalog_open`（含计数器回绕）。spike 参考：12000 首同文件夹约 2.3 s（R1.5 只记录）。
  - **板测清单 B8**：在标签读取范围内，“Storage 单圈最大耗时”有了固件出口：曲名 INFO 的 `us`（M10）、窗口汇总行的 `max` 与 `top`；原判据是单行标签读取上界 < T_data/2，现在只有 1 s 的日志阈值，判据由板级验收清单重新定。B9（扫描耗时）、B10 不在此范围。无主机测试，实际日志与耗时由板级验收票取证。


## 编译期依赖

- 卷事件的 `volume_status()`（`APP/tasks/volume/volume_event.h`，应用可以包含任务的公开合同，ARC-23）。
- Filesystem Service 的目录与文件 Interface、失败分类函数；Log Service；Cortex-M7 周期计数器 Adapter；原生 FreeRTOS 的静态互斥量、tick 与延时。
- `Components/id3_reader`（曲目文本函数）。
- 不包含别的应用，也不包含 `APP/tasks/storage/`。反方向：Storage Task、音频读取任务（`APP/tasks/audio_reader/`，直接包含，不做注入）、两个槽和 `app_tasks.c` 包含本目录的头；`scripts/check-layer-includes.ps1` 的 `app` 种类只拦“应用包含别的应用”，任务包含应用不受限，ARC-23 也只禁应用之间互相包含。

## 保真边界

主机测试是单线程的，通过不等于并发没问题。`Tests/music_catalog` 证明：发布规则、卷序号比对、错误表各分支、读者一次读全的内容、每次调用拿放曲库锁各一次、文件接口与日志不在锁内、拿不到锁时读者等到拿到为止、持锁统计的数字与格式。**不证明**：真实任务抢占与优先级继承、读者与扫描者真实交错时的内存可见性（Cortex-M7 单核加互斥量的屏障）、持锁的真实时长（板上看 `CATLOCK` 行）、12000 首的扫描耗时。这些进板级验收。
