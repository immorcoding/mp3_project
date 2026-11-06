# 存储任务

Storage Task 是**卷事件的订阅者**，继续做音频流、曲库扫描、两个槽（Queue 窗口、曲目显示名）和 Flash 回收；它在迁移到音频读取、前台、后台三个任务的过程中逐步被掏空，最后删除（0.10.0，[ADR-0023](../../../docs/adr/0023-task-split-and-app-contract.md)）。**热插拔、消抖、挂载与卸载、Flash 启动已经不在这里**：它们归卷任务和一次性的 Flash 启动任务（[`APP/tasks/volume/`](../volume/README.md)）。本任务不再碰 Platform SD，也不再有“消抖中”状态：消抖期间文件调用在 Filesystem 入口等闸，已持锁的调用做完，本任务对此一无所知。文件接口不再是唯一调用者（卷任务的对账与它共用 SD 卷锁），所以本任务读到 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 时不当文件错误（见“卷事件与失败约定”）。

## 公开 Interface

- `storage_task.h` 的 `Storage_StatusTypeDef`：本 Task 编排函数的成功/失败。Filesystem 的细粒度结果仍用 `Service_StatusTypeDef`，在 Storage 边界收成 `STORAGE_OK` / `STORAGE_ERROR`。
- `storage_task.h` 的 `Storage_NotifyIndexTypeDef`：本任务私有通知槽。`STORAGE_NOTIFY_EVENT` 由本任务主循环有界等待；卷事件（卷任务或 Flash 启动任务发布卷就绪或卷消失，订阅时登记）置 `STORAGE_NOTIFY_FLAG_VOLUME`，GUI 窗口 `request` 置 `STORAGE_NOTIFY_FLAG_LISTBUFFER`，名字 `request` 置 `STORAGE_NOTIFY_FLAG_NAMEBUFFER`。播放任务写音频流请求信箱置 `STORAGE_NOTIFY_FLAG_STREAM_REQUEST`，归还音频块置 `STORAGE_NOTIFY_FLAG_BLOCK_RETURNED`，均在同一事件槽、与其余 FLAG 共存。本任务只有 `STORAGE_NOTIFY_EVENT` 一个通知槽；SDMMC DMA 与 QSPI/MDMA 的完成通知不借用本任务的槽，由 Filesystem Service 的两个静态二值信号量承担（`InitSD()`/`InitFlash()` 无参数）。
- `storage_task_sd_is_ready()`：SD 卷已挂载**且本任务已处理完这次挂载（曲库扫描完成）**。GUI 可查询；扫描中、卷消失后为否，消抖不再体现在这里。不表示曲库非空。`request` 仅在此时接受。前台一次切之后随 GUI 旧三态一起删除。
- `storage_task_sd_is_mounted()`：卷事件的挂载位（读一个字，不拿锁）。扫描中为真。GUI 用它区分「暂时不能 request」和「卡已拔走该清空 Queue」。前台一次切之后删除，改为 `volume_status()`。
- `storage_task(void *argument)`：由 APP 创建的任务入口，保持 `void` 以满足 `TaskFunction_t`。
- `storage_flash_reclaim()`：Flash 空闲回收，仅供本 Task 调用，返回 `Storage_StatusTypeDef`；只在 Flash 卷已挂载（卷事件的挂载位为真）时调用，否则会在 Flash 卷锁上陪 `MountFlash` 等 3.3 至 8.3 s。回收移进后台任务之前仍在这里。Flash 的启动、挂载与格式化策略在 [`APP/tasks/volume/flash/`](../volume/README.md)。
- 曲库（`catalog_*()`、播放列表、曲目显示名与标签文本、曲目打开、读取诊断）已搬到音乐应用的 [`APP/apps/music/catalog/`](../../apps/music/catalog/README.md)（#212）：一份表、模块内一把短锁、发布时比对卷序号、读者经 `catalog_read_track()` 一次读全代次、条数、路径和定位符；发布规则、错误分类、持锁统计和日志格式都写在那里。本任务是它的调用者：卷序号变了先 `catalog_invalidate()`，卷挂着再 `catalog_init()` 同步扫描（改成单步函数留给后台任务票）；音频流开流、名称槽、窗口槽都经 `catalog_read_track()` 读曲库，再把同一次读到的路径和定位符交给 `catalog_track_open()` / `catalog_track_text_read()`。
- `storage_listbuffer_*()`：Queue 窗口单槽（STOR-6、STOR-16）。GUI Task 仅在 SD 就绪且 `IDLE` 时 `request`（代次/播放列表起点 `Index`/请求条数 → `PENDING` 并通知 Storage）。每行 `Rows[i]` 只放显示文本 {曲目显示名 `Title`, 歌手 `Artist`}（各 252 B，`CATALOG_NAME_BYTES`），不含路径。Storage Task 仅 SD 就绪时 `load`，**每圈最多读一行标签**：每圈先 `catalog_read_track()` 一次读全这一行（首圈核对代次：曲库不可用或与请求代次不符即空窗 `READY`），再经 `catalog_track_text_read()`（内部按定位符打开）填一行；读完前保持 `PENDING`，行间主循环照常处理流请求、名称槽与一次块读，有未填完的窗口时主循环不阻塞等事件。整窗填完才把 `Length` 改成实际条数、写实际代次并打 `READY`，GUI 看不到半窗。途中曲库代次变化（重扫或卷序号变了，读到的代次与开始时不同）以空窗（代次 0）收尾；SD 卷序号变化（卸载、换卡）由 `complete_unavailable()` 同样收尾；消抖期间填到一半的窗口就停在那次文件调用里（调用在 Filesystem 入口等闸，STOR-8），恢复后接着填或随卷序号变化收尾。标签侧失败只令该行回退文件主名、歌手为空，不中断整窗；文件主名也提取不出时该行两列为空串（防御分支）；文件主名提取失败、位置映射失败的单行该行两列为空串，整窗继续（#135 G：窗口内单行失败不中断整窗）；只在列表末尾停止。每圈最坏一次按定位符打开（核对不符多一次按路径打开）+ 取长度 + 至多 `CATALOG_TEXT_MAX_READS` 次读取 + `f_close`（“一行标签读取”，判定见 #135 I 节，板测）。0.8.0 不缓存，每次 `load` 重读。GUI Task 按 `Length` 把两列文本与 `Index` 交给 `Service_GUI_QueueApply()` 后写回 `IDLE`。`Index` 随 `QueueTab` 竖滑移动，不是条数。`Generation` 须与曲库代次一致，`0` 为作废空窗。当前播放游标不在本槽，由播放任务持有（ADR-0020）。`Service/gui` 不得包含此头。`STORAGE_LISTBUFFER_MAX_ENTRIES` 定义在 `storage_listbuffer.h`。
- `storage_namebuffer_*()`：曲目显示名单槽（STOR-15），与 Queue 窗口槽分开、互不争用，三态同上。GUI 音乐分区在 `IDLE` 时 `request`（播放列表位置 `Index`、代次，0 表示“当前”）→ `PENDING` 并置 `STORAGE_NOTIFY_FLAG_NAMEBUFFER`；`request` 不检查 SD 是否就绪，只在 Storage Task 尚未 `bind` 或槽位不是 `IDLE` 时被拒。Storage Task 每圈 `load`：SD 就绪、曲库可用（`catalog_read_track()` 一次读全，代次非 0）、请求代次为 0 或与当前相符、位置不越界时，经 `catalog_track_text_read()`（内部按定位符打开）读曲目标签，把曲目显示名（可用 TIT2，否则文件主名）写进 252 B `Title`、歌手（可用 TPE1，否则空串）写进 252 B `Artist`，写入实际代次后打 `READY`；一次 `load` 只读一个文件，在当圈完成。标签侧失败（打开失败、读取错误、超读取上限、无 ID3v2、帧缺失、解码失败）不是槽失败：照常带实际代次 `READY`，标题回退文件主名、歌手为空。其余情形 `READY` 且 `Generation = 0`、空名。GUI 读完写回 `IDLE`。代次不符、SD 未就绪、请求“当前”时越界（空库开播）是正常竞态，不记日志；代次相符（或请求“当前”）文件主名仍提取失败、或代次非 0 相符却越界，是防御分支，记一条 WARN `Name fail idx=N gen=G`（N 为请求位置，G 为请求代次），不写名字（DIAG-3）。拔卡或离开就绪时 `complete_unavailable()` 收掉 `PENDING`。只允许 GUI 音乐分区包含，`Service/gui` 不得包含此头。
- `storage_audio_stream_*()` / `storage_audio_block_*()`：音频流分区 `stream/`，见下文“音频流分区”。
- 三个基准（SD 文件读写、SDRAM 破坏性自检、W25Q256 物理与文件）已随 `benchmark/` 搬到 [`APP/tasks/volume/benchmark/`](../volume/benchmark/README.md)：SDRAM 自检在 Boot 任务里、创建本任务之前运行，SD 基准由卷任务在发卷就绪之前跑，Flash 基准由 Flash 启动任务跑。

## 编译期依赖

- 卷事件（`APP/tasks/volume/volume_event.h`：`volume_status()`、`volume_subscribe()`）。不再包含 `platform_sd.h`。
- Filesystem Service 的卷感知文件与目录 Interface 和失败分类函数（`Service_Filesystem_StatusIsVolumeUnavailable()`）；Flash 回收的生命周期入口。
- Log Service 投递 Interface。
- Cortex-M7 周期计数器 Adapter（标签读取诊断只读 DWT）。
- 曲库模块 [`APP/apps/music/catalog/`](../../apps/music/catalog/README.md)（`catalog.h`、`catalog_track_open.h`、`catalog_track_text.h`、`catalog_track_diag.h`、`catalog_name.h`）；窗口汇总阈值和单行日志阈值在它的 `catalog_config.h`。
- 仅用于主循环事件 FLAG（含卷事件）的原生 FreeRTOS 索引任务通知 Interface。

## 运行时请求与事件路径

卡检测 GPIO EXTI 现在通知的是**卷任务**（它的 ISR 回调只发一个任务通知），不再通知本任务。本任务在入口 `volume_subscribe(task, STORAGE_NOTIFY_EVENT, STORAGE_NOTIFY_FLAG_VOLUME)` 登记为卷事件的订阅者：卷任务（SD）或 Flash 启动任务（Flash）发布卷就绪或卷消失时，先更新卷状态字再对本任务的通知槽置位。GUI 窗口与名字 `request` 分别置同一槽的 `STORAGE_NOTIFY_FLAG_LISTBUFFER`、`STORAGE_NOTIFY_FLAG_NAMEBUFFER`。Filesystem 私有 DMA 执行器在调用者（本 Task，或卷任务的刷新与挂载）持 SD 卷锁时的上下文等待 SD 完成信号量；QSPI/MDMA 的 Flash 路径同理，在持 Flash 卷锁的调用者（Flash 启动任务、本任务的回收）上下文等待。

`storage_task()` 主循环维护就绪 / 未就绪，有界等待 `STORAGE_NOTIFY_EVENT`。**每圈先比对 SD 卷序号**（一个字，没变就立刻返回，所以丢叫醒、合并叫醒、晚订阅都不会漏）：序号变了，旧的全部作废，再看现状（见“卷事件与失败约定”）。就绪（卷挂着且曲库扫描完成）时才 `load` 窗口（每圈至多一行，窗口未填完时等待时长为 0）。回收：Flash 卷已挂载且没有打开的音频流、100 ms 周期到点才 `storage_flash_reclaim()`；Flash 卷未挂载（Flash 启动任务在 `MountFlash`）时不回收，等 Flash 卷就绪的卷事件。FTL 决定是否回收，最长一次不可抢占擦除仍需板测。一次 Service 文件读写会在调用者的嵌套调用栈中进入 Filesystem 的 SD DMA 执行器，等待 SD 完成信号量、收尾当前分块后再启动下一分块。ISR 只发布事件，绝不复制数据、维护 Cache 或启动下一笔传输。

### 卷事件与失败约定

- **订阅**：`storage_task()` 入口登记订阅，并先比对一次序号（`storage_task_apply_sd_volume()`），所以本任务比卷任务晚启动也不会漏。
- **序号变了**（卸载、换卡的序号跳 2、被吞的拔出加重新挂载）：① `storage_audio_stream_card_removed()`：当前流仍被播放任务持有（含已读完的流）就写 `CARD_REMOVED`；② 作废曲库（`catalog_invalidate()`）；③ 收掉 Queue 窗口槽和曲目显示名槽（PENDING 以空窗/空名收尾，**已经 READY 但 GUI 还没取走的也收成空窗/空名、代次 0**）；④ 卷挂着就扫描曲库（`catalog_init()`），扫描完成且序号未再变才置就绪。卷任务不等本任务：卸载已使旧令牌失效，晚一点收尾也碰不到旧卡。
- **`NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 不是文件错误**（`Service_Filesystem_StatusIsVolumeUnavailable()`）：音频流读块得到它们时，先对同一个句柄 `CloseFile`（它在 Filesystem 入口等闸，等到本轮对账结束），再看 SD 卷：序号变了或卷已不挂是卡没了，不写 `READ_ERROR`、不重试，流“停住”（FIL 已关，不空转），等卷事件写 `CARD_REMOVED`——这就是“在途读取被拔卡打断时显示 No SD card、游标重置”；**序号没变且卷仍挂着是卡还在、设备出错，没有卷事件可等，立刻按读错误报**（`storage_audio_classify_read()`）；开流或探测得到它们按 `NOT_READY` 应答；曲库扫描得到它们丢弃本次扫描、不发布、不记 ERROR（日志 `Scan done res=aborted`；打开目录先再开一次，等过本轮对账才下结论；扫描途中或发布前卷序号变了也同样丢弃），`catalog_scan_verdict(volume_unchanged)` 给出三种结果：完成照常就绪；卷变了等卷事件重扫、不置就绪；卷没变（卡还在、读不了）则记 ERROR、置就绪、曲库为空，拔插卡后重扫；曲目标签读取打开时得到它们返回空结果，不回退文件主名、不计“打开失败”，名字槽也不为此记 `Name fail` WARN（`catalog_track_text_last_read_unavailable()`）。其他错误表示卡还在，立刻按原来的方式报错。
- **已知漏洞**（推断，板上没测到）：数据脚比检测脚先断时，第一次失败仍是 `ERROR`，显示读取错误而不是 No SD card，同样会停播。

## 音频流分区

`stream/` 把 SD 上的 MP3 以“音频块”交给播放任务（本分区只定义 Storage 一侧与调用契约，播放任务见 `APP/tasks/playback/`）。FIL 的打开、读取、关闭（含拔卡）都在 Storage Task（ARC-12）；播放任务只持流序号。

- 文件：`storage_audio_stream.*`（信箱、开启、读块、终止记录）、`storage_audio_result.*`（文件调用结果的分类，纯函数，主机测试见 `Tests/storage_audio_stream/`：卷不可用的三种返回值读块时“停住”、开流时按 `NOT_READY` 应答）、`storage_audio_block.*`（块池与两个队列）、`storage_audio_range.*`（越过标签纯函数，主机测试见 `Tests/storage_audio_stream/`）、`storage_audio_config.h`（块长 32 KiB、块数 32，均为配置常量）。
- 请求信箱（播放任务 → Storage）：长度 1 的覆盖队列，内容 `{StreamSeq, Generation, Position, AudioOffset}`，`StreamSeq` 为 0 表示无流；开启、关闭、取消是同一种写入，经 `storage_audio_stream_request()` 写入并置 `STORAGE_NOTIFY_FLAG_STREAM_REQUEST`。代次 0 表示“当前”。`AudioOffset`（[#176](https://github.com/immorcoding/mp3_project/issues/176)，ADR-0021）是相对音频区间起点的字节数：切歌与普通开流填 0，跳转重开流填 Service/playback 映射给出的偏移。Storage 不认识音频格式，只经纯函数 `storage_audio_range_seek_position()` 把它钳到区间内（区间内原样换算，越过区间钳在区间末端，空区间停在起点，任意 32 位偏移不回绕），再 `f_lseek` 到区间起点加偏移；应答的 `AudioBytes` 仍是整段区间长度，剩余字节数 = 区间末端 − 落点。偏移被钳到末端时没有字节可读，开启应答同时带 `End = ENDED`（不是读错误、不占块）。开启与跳转重开流都经 `catalog_track_open()` 按文件定位符打开，核对不符退回按路径打开（[#179](https://github.com/immorcoding/mp3_project/issues/179)，STOR-17）；打开成功后记一行 `Stream` INFO（见上）。
- 流记录信箱（Storage → 播放任务）：长度 1 的覆盖队列，内容 `{StreamSeq, OpenResult, Generation, TrackCount, AudioBytes, End}`，整份覆盖，开启参数一直保留；播放任务用 `storage_audio_stream_get_record()` 非消费读取。开启结果：OK、NOT_READY、STALE、NO_TRACK、BAD_FILE（打不开、标签长度越界、音频区间为空）、IO_ERROR；终止：ENDED（只在最后一块入队之后写）、READ_ERROR、CARD_REMOVED。Service 无法区分“文件不存在”与“打开时 I/O 错误”，打开失败一律 BAD_FILE；位置超出列表按 STALE。BAD_FILE 时记录仍带 Generation 与 TrackCount（AudioBytes 为 0），其他失败三者皆 0。
- 块：`{StreamSeq, ValidBytes}` 加数据，有效字节数 1..块容量，任何块都可以是短块，不设 kind、末块标志或块序号。Storage 只用当前正在读的流序号盖章；读完后若请求信箱已有新请求，半读的块不入就绪队列，直接放回空闲队列。播放任务用 `storage_audio_block_take_ready()` / `peek_ready()` 取块，用 `storage_audio_stream_return_block()` 归还（放回空闲队列并置 `STORAGE_NOTIFY_FLAG_BLOCK_RETURNED`）。
- 唤醒播放任务：本分区不认识播放任务的事件槽，由播放任务外壳用 `storage_audio_stream_set_wake_callback()` 注册回调（`STORAGE_AUDIO_WAKE_STREAM_RECORD`、`STORAGE_AUDIO_WAKE_BLOCK_READY`），回调只能置 FLAG。FLAG 只用于唤醒，醒来后以信箱与队列内容为准。播放任务须在 `storage_task()` 完成 `storage_audio_stream_init()` 之后才能调用 `request`（此前返回 false）。
- 主循环每圈顺序：卷事件 → 请求信箱 → 列表窗口（至多一行标签读取）→ 曲目显示名（至多一个文件的标签读取）→ 最多读一块 → 回收。还有工作（待处理请求，或流已打开且有空闲块，或就绪时窗口未填完）时不阻塞等待。结构上界：取消 ≤ 一次块读取；应答 ≤ 一行标签读取（窗口逐圈一行，STOR-16）；开启 ≤ 按定位符打开（目标约 ≤ 30 ms，与文件在文件夹内的位置无关）+ 标签探测（读头、读尾、seek）+ 一次块读取。
- 有打开的流时（FIL 未关）不提供 Flash 回收机会，回收剩余不参与等待计时；流关闭（ENDED、读错、拔卡、无流请求）后照常，暂停期间因流保持而顺延。
- 消抖期：本任务不知道消抖。文件调用在 Filesystem 入口等闸，所以开流和补块的那次调用慢约 30 ms 后正常返回（卡还在），或卸载后返回 `NOT_READY` / `INVALID_HANDLE`（卡已拔，流停住，随后的卷事件写 `CARD_REMOVED`）；已打开的流保持打开。
- 拔卡：本任务处理卷序号变化时，在作废曲库之前先 `storage_audio_stream_card_removed()`（此时卷任务已经卸载，令牌已失效，`CloseFile` 只返回 `INVALID_HANDLE`，无害）：按“当前流”而非“文件是否打开”判断：Storage 已为某个非 0 流序号发布过记录、播放任务尚未以无流请求或新流序号释放它，就先关闭 FIL（仍打开时），再写 `{当前流序号, CARD_REMOVED}` 并唤醒播放任务；因此文件已读完（ENDED）而播放任务仍在从内存放音时拔卡同样立即停止。没有当前流（从未开启、开启失败、已被无流请求释放）、或已以 READ_ERROR / CARD_REMOVED 终止时不写、不唤醒。若拔卡处理时仍有新流请求排队（命令紧跟在拔卡后几毫秒内），旧记录照常写 CARD_REMOVED，播放任务因该流已释放而忽略它，新请求的开启以 NOT_READY 结束，播放仍会停止；此情形可接受。换卡时序号跳 2，同样走这里（换卡视同拔卡）。
- 块池内存与 HWD-2 审查：块池 32 × 32 KiB = 1 MiB 放 SDRAM NOLOAD 段，块内容入队才有效，不依赖清零，**块只由 CPU 读写，不是 DMA 目标**（SD DMA → bounce buffer → memcpy 进块 → 播放任务 memcpy 进窗口）。
  - 启动：`storage_audio_block_init()` 在 `storage_task()` 内调用，晚于 Boot 任务里可选的 `storage_sdram_benchmark_run()` 全容量诊断（诊断在创建本任务之前就跑完），只建队列并入队块指针，不触碰块内容；SDRAM 在 `main()` 经 `Platform/sdram` 完成正式初始化后任务才运行，满足“正式初始化之后、首次使用前”。队列控制块与存储放 DTCM `.bss`（约 416 B）。
  - MPU：`Core/Src/main.c` 的 Region 2 把 0xC0000000 起 32 MB 设为 Cacheable、Bufferable、不可执行、全访问，块池落其中；块只由单核 CPU 两个任务读写，无 DMA 与 Cache 一致性问题，ARC-18 不适用。
  - 全容量诊断：诊断是破坏性的，只能在块池入队前独占运行；`STORAGE_SDRAM_BENCHMARK_ENABLE` 开启时由 Boot 任务在创建本任务之前运行（栈核算见 [`APP/tasks/volume/README.md`](../volume/README.md)），产品默认关闭。
  - 链接段：块池在链接脚本 `STM32H743xG_flash.ld` 的独立段 `.storage_audio_pool` (NOLOAD, >SDRAM)，位于 `.sdram_framebuffer` 之后，不移动既有 SDRAM 段；段名由 `storage_audio_config.h` 的 `STORAGE_AUDIO_POOL_SECTION` 持有（用户授权，链接脚本由用户手改）。 `hw:pending`：上板确认 map 中块池落点、SDRAM 总用量在容量内。
- 板级行为（单圈耗时、消抖、拔卡、播放中不回收、1 MiB 缓冲下的断流余量）归板级验收票，Storage 音频流主循环（含拔卡写记录）不做主机测试，由板级验收覆盖；主机只在 `Tests/playback_task` 钉住播放侧前提（ENDED 后记录变 CARD_REMOVED 立即停止），保真边界止于此。

## 约束

- `STORAGE_NOTIFY_EVENT` 属于本 Module：卷事件置 `STORAGE_NOTIFY_FLAG_VOLUME`，主循环每圈比对卷序号。GUI 窗口请求置 `STORAGE_NOTIFY_FLAG_LISTBUFFER`，且仅 SD 已挂载时 `request` 成功；名字请求置 `STORAGE_NOTIFY_FLAG_NAMEBUFFER`，任何 SD 状态下都可提交，未就绪时按代次 0 应答。这些 FLAG 不得拆成轮流阻塞的独立槽。SD 就绪由挂载事实决定，不由播放列表代次决定。
- SD 完成信号量不由本任务主循环消费。它属于 Filesystem Service 的同步 SDMMC DMA 执行器；执行器在调用者（Storage Task，或卷任务的刷新与挂载）上下文、于 FatFs 读写期间等待它，超时后先显式中止在飞的 DMA（有上界，停不住就反初始化、卷保持 ERROR），再清掉迟到的旧信号，下一次启动传输前还会再清一次（见 [Filesystem Service](../../../Service/filesystem/README.md) 的“显式中止”）。
- Flash 完成信号量属于 Filesystem Service Flash 执行器。它提示一个 QSPI 异步操作的完成、状态匹配、错误或中止；Flash 物理 benchmark 经 `Service_Filesystem_ReadFlashArray()` 等诊断入口等待该信号量并触发 `Platform_Flash_ProcessOperation()` 判定结果。读取信号不能直接视为数据可用，写擦的状态匹配信号也不能直接视为 Device 已恢复 READY。
- APP 不注册 SDMMC 传输回调，不使用 `BSP_SD_*`、不调用 `HAL_SD_*`，也不访问 `hsd1` 或 DMA bounce buffer，也不再包含 `platform_sd.h`；卡检测回调、刷新、挂载与卸载都经 Filesystem Service 的入口，由卷任务调用。
- 插卡只有在 Platform SD 报告 `READY` 后才由卷任务挂载并发卷就绪，本任务据此扫描曲库并生成顺序播放列表；拔卡由卷任务先卸载（令牌失效、槽释放）再发卷消失，本任务据此作废曲库与播放列表。设备不格式化 SD；未格式化只记录告警。
- `storage_task_config.h` 保存卷事件标志位和 Flash 回收周期；它不是对其他 Task 的公开 Interface。卡检测消抖的静默窗口现在是卷任务的 `VOLUME_SD_DEBOUNCE_MS`（`APP/tasks/volume/volume_task_config.h`）。三个基准的开关与配置见 [`APP/tasks/volume/benchmark/README.md`](../volume/benchmark/README.md)。

## 命名

Task 内部 Implementation 使用 `storage_*`（0.10.0 里 Storage 前缀随各部分迁移逐步改名）；任务入口保持 `storage_task()`；跨层调用使用 `Service_Filesystem_*` 与 `Service_Log_*`。

## FTL 集成（已实现，待上板验收）

Flash 的启动不再在本任务里：卷任务创建一次性的 Flash 启动任务（优先级 2，静态栈放 AXI 新段），由它 `InitFlash()` 建立执行器、按策略挂载已有卷、`SERVICE_NO_FILESYSTEM` 时按 `VOLUME_FLASH_AUTO_FORMAT` 格式化，成功后发 Flash 卷就绪并自删；物理基准在执行器绑定之后、挂载之前运行，文件基准仅在挂载成功后运行，详见 [`APP/tasks/volume/README.md`](../volume/README.md)。QSPI 回调、Flash 完成信号量的等待和传输收尾职责属于 Filesystem Service 私有 Flash 执行器。

本任务只剩回收：主循环为 `ReclaimFlash` 安排定期机会（Flash 卷已挂载、没有打开的音频流时），不再无限期只等 SD 检测。FTL 决定是否 GC、回收哪个块；已在飞操作按硬件通知与超时推进。默认回收机会周期 100 ms，每次最多回收一块，不承诺 NOR 擦除可以立即抢占。SDRAM 破坏性自检必须早于 FTL 表与业务缓冲使用（现在在 Boot 任务里，早于所有任务）。
