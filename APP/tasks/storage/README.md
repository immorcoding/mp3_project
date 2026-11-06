# 存储任务

Storage Task 是**卷事件的订阅者**，继续做曲库扫描、两个槽（Queue 窗口、曲目显示名）和 Flash 回收；它在迁移到音频读取、前台、后台三个任务的过程中逐步被掏空，最后删除（0.10.0，[ADR-0023](../../../docs/adr/0023-task-split-and-app-contract.md)）。**音频流已经搬到优先级 3 的音频读取任务**（[`APP/tasks/audio_reader/`](../audio_reader/README.md)，#215）：请求信箱、流记录信箱、块池、开流、读块和 `CARD_REMOVED` 都不在这里了，本任务也不再有 `stream/` 目录。**热插拔、消抖、挂载与卸载、Flash 启动已经不在这里**：它们归卷任务和一次性的 Flash 启动任务（[`APP/tasks/volume/`](../volume/README.md)）。本任务不再碰 Platform SD，也不再有“消抖中”状态：消抖期间文件调用在 Filesystem 入口等闸，已持锁的调用做完，本任务对此一无所知。文件接口不再是唯一调用者（卷任务的对账与它共用 SD 卷锁），所以本任务读到 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 时不当文件错误（见“卷事件与失败约定”）。

## 公开 Interface

- `storage_task.h` 的 `Storage_StatusTypeDef`：本 Task 编排函数的成功/失败。Filesystem 的细粒度结果仍用 `Service_StatusTypeDef`，在 Storage 边界收成 `STORAGE_OK` / `STORAGE_ERROR`。
- `storage_task.h` 的 `Storage_NotifyIndexTypeDef`：本任务私有通知槽。`STORAGE_NOTIFY_EVENT` 由本任务主循环有界等待；卷事件（卷任务或 Flash 启动任务发布卷就绪或卷消失，订阅时登记）置 `STORAGE_NOTIFY_FLAG_VOLUME`，GUI 窗口 `request` 置 `STORAGE_NOTIFY_FLAG_LISTBUFFER`，名字 `request` 置 `STORAGE_NOTIFY_FLAG_NAMEBUFFER`。音频流请求信箱和块归还已改叫醒音频读取任务（它有自己的通知槽），不再经本任务。本任务只有 `STORAGE_NOTIFY_EVENT` 一个通知槽；SDMMC DMA 与 QSPI/MDMA 的完成通知不借用本任务的槽，由 Filesystem Service 的两个静态二值信号量承担（`InitSD()`/`InitFlash()` 无参数）。
- `storage_task_sd_is_ready()`：SD 卷已挂载**且本任务已处理完这次挂载（曲库扫描完成）**。GUI 可查询；扫描中、卷消失后为否，消抖不再体现在这里。不表示曲库非空。`request` 仅在此时接受。前台一次切之后随 GUI 旧三态一起删除。
- `storage_task_sd_is_mounted()`：卷事件的挂载位（读一个字，不拿锁）。扫描中为真。GUI 用它区分「暂时不能 request」和「卡已拔走该清空 Queue」。前台一次切之后删除，改为 `volume_status()`。
- `storage_task(void *argument)`：由 APP 创建的任务入口，保持 `void` 以满足 `TaskFunction_t`。
- `storage_flash_reclaim()`：Flash 空闲回收，仅供本 Task 调用，返回 `Storage_StatusTypeDef`；只在 Flash 卷已挂载（卷事件的挂载位为真）时调用，否则会在 Flash 卷锁上陪 `MountFlash` 等 3.3 至 8.3 s。回收移进后台任务之前仍在这里。Flash 的启动、挂载与格式化策略在 [`APP/tasks/volume/flash/`](../volume/README.md)。
- 曲库（`catalog_*()`、播放列表、曲目显示名与标签文本、曲目打开、读取诊断）已搬到音乐应用的 [`APP/apps/music/catalog/`](../../apps/music/catalog/README.md)（#212）：一份表、模块内一把短锁、发布时比对卷序号、读者经 `catalog_read_track()` 一次读全代次、条数、路径和定位符；发布规则、错误分类、持锁统计和日志格式都写在那里。本任务是它的调用者：卷序号变了先 `catalog_invalidate()`，卷挂着再 `catalog_init()` 同步扫描（改成单步函数留给后台任务票）；音频流开流、名称槽、窗口槽都经 `catalog_read_track()` 读曲库，再把同一次读到的路径和定位符交给 `catalog_track_open()` / `catalog_track_text_read()`。
- `storage_listbuffer_*()`：Queue 窗口单槽（STOR-6、STOR-16）。GUI Task 仅在 SD 就绪且 `IDLE` 时 `request`（代次/播放列表起点 `Index`/请求条数 → `PENDING` 并通知 Storage）。每行 `Rows[i]` 只放显示文本 {曲目显示名 `Title`, 歌手 `Artist`}（各 252 B，`CATALOG_NAME_BYTES`），不含路径。Storage Task 仅 SD 就绪时 `load`，**每圈最多读一行标签**：每圈先 `catalog_read_track()` 一次读全这一行（首圈核对代次：曲库不可用或与请求代次不符即空窗 `READY`），再经 `catalog_track_text_read()`（内部按定位符打开）填一行；读完前保持 `PENDING`，行间主循环照常处理流请求、名称槽与一次块读，有未填完的窗口时主循环不阻塞等事件。整窗填完才把 `Length` 改成实际条数、写实际代次并打 `READY`，GUI 看不到半窗。途中曲库代次变化（重扫或卷序号变了，读到的代次与开始时不同）以空窗（代次 0）收尾；SD 卷序号变化（卸载、换卡）由 `complete_unavailable()` 同样收尾；消抖期间填到一半的窗口就停在那次文件调用里（调用在 Filesystem 入口等闸，STOR-8），恢复后接着填或随卷序号变化收尾。标签侧失败只令该行回退文件主名、歌手为空，不中断整窗；文件主名也提取不出时该行两列为空串（防御分支）；文件主名提取失败、位置映射失败的单行该行两列为空串，整窗继续（#135 G：窗口内单行失败不中断整窗）；只在列表末尾停止。每圈最坏一次按定位符打开（核对不符多一次按路径打开）+ 取长度 + 至多 `CATALOG_TEXT_MAX_READS` 次读取 + `f_close`（“一行标签读取”，判定见 #135 I 节，板测）。0.8.0 不缓存，每次 `load` 重读。GUI Task 按 `Length` 把两列文本与 `Index` 交给 `Service_GUI_QueueApply()` 后写回 `IDLE`。`Index` 随 `QueueTab` 竖滑移动，不是条数。`Generation` 须与曲库代次一致，`0` 为作废空窗。当前播放游标不在本槽，由播放任务持有（ADR-0020）。`Service/gui` 不得包含此头。`STORAGE_LISTBUFFER_MAX_ENTRIES` 定义在 `storage_listbuffer.h`。
- `storage_namebuffer_*()`：曲目显示名单槽（STOR-15），与 Queue 窗口槽分开、互不争用，三态同上。GUI 音乐分区在 `IDLE` 时 `request`（播放列表位置 `Index`、代次，0 表示“当前”）→ `PENDING` 并置 `STORAGE_NOTIFY_FLAG_NAMEBUFFER`；`request` 不检查 SD 是否就绪，只在 Storage Task 尚未 `bind` 或槽位不是 `IDLE` 时被拒。Storage Task 每圈 `load`：SD 就绪、曲库可用（`catalog_read_track()` 一次读全，代次非 0）、请求代次为 0 或与当前相符、位置不越界时，经 `catalog_track_text_read()`（内部按定位符打开）读曲目标签，把曲目显示名（可用 TIT2，否则文件主名）写进 252 B `Title`、歌手（可用 TPE1，否则空串）写进 252 B `Artist`，写入实际代次后打 `READY`；一次 `load` 只读一个文件，在当圈完成。标签侧失败（打开失败、读取错误、超读取上限、无 ID3v2、帧缺失、解码失败）不是槽失败：照常带实际代次 `READY`，标题回退文件主名、歌手为空。其余情形 `READY` 且 `Generation = 0`、空名。GUI 读完写回 `IDLE`。代次不符、SD 未就绪、请求“当前”时越界（空库开播）是正常竞态，不记日志；代次相符（或请求“当前”）文件主名仍提取失败、或代次非 0 相符却越界，是防御分支，记一条 WARN `Name fail idx=N gen=G`（N 为请求位置，G 为请求代次），不写名字（DIAG-3）。拔卡或离开就绪时 `complete_unavailable()` 收掉 `PENDING`。只允许 GUI 音乐分区包含，`Service/gui` 不得包含此头。
- `audio_reader_stream_*()` / `audio_reader_block_*()`：音频流已搬到 [`APP/tasks/audio_reader/`](../audio_reader/README.md)，本任务不再提供。
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

`storage_task()` 主循环维护就绪 / 未就绪，有界等待 `STORAGE_NOTIFY_EVENT`。**每圈先比对 SD 卷序号**（一个字，没变就立刻返回，所以丢叫醒、合并叫醒、晚订阅都不会漏）：序号变了，旧的全部作废，再看现状（见“卷事件与失败约定”）。就绪（卷挂着且曲库扫描完成）时才 `load` 窗口（每圈至多一行，窗口未填完时等待时长为 0）。回收：Flash 卷已挂载、100 ms 周期到点才 `storage_flash_reclaim()`（打开的音频流不再暂停回收，STOR-12；音频读取在另一个任务、SD 与 Flash 各一把卷锁，擦除不挡补块）；Flash 卷未挂载（Flash 启动任务在 `MountFlash`）时不回收，等 Flash 卷就绪的卷事件。FTL 决定是否回收，最长一次不可抢占擦除仍需板测。一次 Service 文件读写会在调用者的嵌套调用栈中进入 Filesystem 的 SD DMA 执行器，等待 SD 完成信号量、收尾当前分块后再启动下一分块。ISR 只发布事件，绝不复制数据、维护 Cache 或启动下一笔传输。

### 卷事件与失败约定

- **订阅**：`storage_task()` 入口登记订阅，并先比对一次序号（`storage_task_apply_sd_volume()`），所以本任务比卷任务晚启动也不会漏。
- **序号变了**（卸载、换卡的序号跳 2、被吞的拔出加重新挂载）：① 作废曲库（`catalog_invalidate()`）；② 收掉 Queue 窗口槽和曲目显示名槽（PENDING 以空窗/空名收尾，**已经 READY 但 GUI 还没取走的也收成空窗/空名、代次 0**）；③ 卷挂着就扫描曲库（`catalog_init()`），扫描完成且序号未再变才置就绪。音频流的 `CARD_REMOVED` 由音频读取任务自己写（它也订阅卷事件，与本任务不分先后；曲库代次在卷序号变了之后就读作 0，所以不会有人按旧曲库开新流）。卷任务不等本任务：卸载已使旧令牌失效，晚一点收尾也碰不到旧卡。
- **`NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 不是文件错误**（`Service_Filesystem_StatusIsVolumeUnavailable()`）：音频流读块、开流的处理（先对同一个句柄 `CloseFile` 等过本轮对账，再看卷序号）已经随音频读取任务搬走，见 [`APP/tasks/audio_reader/README.md`](../audio_reader/README.md)“失败约定”；曲库扫描得到它们丢弃本次扫描、不发布、不记 ERROR（日志 `Scan done res=aborted`；打开目录先再开一次，等过本轮对账才下结论；扫描途中或发布前卷序号变了也同样丢弃），`catalog_scan_verdict(volume_unchanged)` 给出三种结果：完成照常就绪；卷变了等卷事件重扫、不置就绪；卷没变（卡还在、读不了）则记 ERROR、置就绪、曲库为空，拔插卡后重扫；曲目标签读取打开时得到它们返回空结果，不回退文件主名、不计“打开失败”，名字槽也不为此记 `Name fail` WARN（`catalog_track_text_last_read_unavailable()`）。其他错误表示卡还在，立刻按原来的方式报错。
- **已知漏洞**（推断，板上没测到）：数据脚比检测脚先断时，第一次失败仍是 `ERROR`，显示读取错误而不是 No SD card，同样会停播。

## 音频流

音频流分区（请求信箱、流记录信箱、块池、开流、读块、拔卡写 `CARD_REMOVED`、`SERVICE_BUSY` 重试）已经在 #215 搬到音频读取任务，见 [`APP/tasks/audio_reader/README.md`](../audio_reader/README.md)；`stream/` 目录和 `storage_audio_*` 这些名字不再存在。本任务不再订阅音频流的请求和块归还，也不再因为“有打开的音频流”而暂停 Flash 回收（STOR-12）。

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

本任务只剩回收：主循环为 `ReclaimFlash` 安排定期机会（Flash 卷已挂载时；打开的音频流不再影响它，STOR-12），不再无限期只等 SD 检测。FTL 决定是否 GC、回收哪个块；已在飞操作按硬件通知与超时推进。默认回收机会周期 100 ms，每次最多回收一块，不承诺 NOR 擦除可以立即抢占。SDRAM 破坏性自检必须早于 FTL 表与业务缓冲使用（现在在 Boot 任务里，早于所有任务）。
