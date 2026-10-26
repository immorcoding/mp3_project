# 存储任务

Storage Task 是 SD 热插拔生命周期决策和 FatFs 卷访问的唯一任务上下文。GPIO EXTI 边沿只表示“稍后重新检查”，并不直接表示“已经插卡”或“已经拔卡”。`sd/` 与 `flash/` 是本 Task 的私有编排分区：任务入口只做启动顺序、SD 状态机、截止时刻等待和空闲回收时机。

## 公开 Interface

- `storage_task.h` 的 `Storage_StatusTypeDef`：本 Task 编排函数的成功/失败。Filesystem 的细粒度结果仍用 `Service_StatusTypeDef`，在 Storage 边界收成 `STORAGE_OK` / `STORAGE_ERROR`。
- `storage_task.h` 的 `Storage_NotifyIndexTypeDef`：本任务私有通知槽。`STORAGE_NOTIFY_EVENT` 由本任务主循环有界等待；卡检测边沿置 `STORAGE_NOTIFY_FLAG_SD_DETECT`，GUI `request` 置 `STORAGE_NOTIFY_FLAG_LISTBUFFER`。播放任务写音频流请求信箱置 `STORAGE_NOTIFY_FLAG_STREAM_REQUEST`，归还音频块置 `STORAGE_NOTIFY_FLAG_BLOCK_RETURNED`，均在同一事件槽、与前两个 FLAG 共存。`STORAGE_NOTIFY_SD_TRANSFER` 与 `STORAGE_NOTIFY_FLASH_OPERATION` 在 `InitSD`/`InitFlash` 时注入 Filesystem Service，不与主循环事件槽共用。
- `storage_task_sd_is_ready()`：SD FatFs 卷已挂载且不在消抖中。GUI 可查询；消抖中与未就绪均为否。不表示曲库非空。`request` 仅在此时接受。
- `storage_task_sd_is_mounted()`：FatFs 卷对象仍在。消抖中只要尚未卸载也为真。GUI 用它区分「暂时不能 request」和「卡已拔走该清空 Queue」。
- `storage_task(void *argument)`：由 APP 创建的任务入口，保持 `void` 以满足 `TaskFunction_t`。
- `storage_sd_*()`：SD 热插拔与挂载策略，仅供本 Task 调用；返回 `Storage_StatusTypeDef`。
- `storage_flash_init()` / `storage_flash_reclaim()`：Flash 启动、挂载策略与空闲回收，仅供本 Task 调用，返回 `Storage_StatusTypeDef`。`storage_flash_init()` 必须传入当前 Storage Task 句柄，句柄为空或与当前任务不符时拒绝。
- `storage_catalog_*()`：曲库扫描与作废，返回 `Storage_StatusTypeDef`。曲库是 SD `Music/` 的路径事实表（SDRAM `.storage_catalog` 字符串池 + 偏移/长度，不存卷字段）；公开头不暴露表结构。启动不把该段并入 `.bss` 清零；扫描前只重置表头。没有 `Music/` 时为空表成功。条数满或池满则截断已收录部分，仍 `STORAGE_OK`。`storage_catalog_books_init()` 是空桩，恒成功，当前不调用。拔卡先 `storage_catalog_invalidate()`，再卸载。技术事实见 [storage.catalog.md](../../../docs/shape/storage.md#catalog)。
- `storage_sheet_*()`：播放列表，与曲库同属 `catalog/`。首版是恒等下标序列 `SeqList[i] = i`，有效长度即建表时的 Catalog `IndexNum`，不另存 Count。`Generation` 记录对应的 Catalog 代次，`0` 表示已作废。表在 SDRAM `.music_sheet`（NOLOAD），作废不清整数组。曲库扫描成功后立即建表；`storage_catalog_invalidate()` 会一并作废。不向 GUI 暴露整表指针。随机/心动序列尚未实现。
- `storage_catalog_display_name()`：曲目显示名纯函数（`catalog/storage_catalog_name.h`），由曲库 UTF-8 相对路径取最后一个 `/` 之后的文件主名并去掉末尾 `.mp3`（大小写同扫描），主名超过 251 B 或非法 UTF-8 返回 `false`，不截断。无 RTOS 依赖，主机测试见 `Tests/storage_catalog`；尚未接入名称槽。
- `storage_listbuffer_*()`：Queue 窗口单槽。GUI Task 仅在 SD 就绪且 `IDLE` 时 `request`（代次/播放列表起点 `Index`/请求条数 → `PENDING` 并通知 Storage）；Storage Task 仅 SD 就绪时 `load` 填路径拷贝，把 `Length` 改成实际条数后打 `READY`。GUI Task 按 `Length` 把路径和 `Index` 交给 `Service_GUI_QueueApply()` 后写回 `IDLE`。`Index` 随 `QueueTab` 竖滑移动，不是条数。`Generation` 须与 Catalog/Sheet 一致，`0` 为作废空窗。当前播放游标不在本槽，由播放任务持有（ADR-0020）。`Service/gui` 不得包含此头。宏见 `storage_catalog_config.h`。
- `storage_audio_stream_*()` / `storage_audio_block_*()`：音频流分区 `stream/`，见下文“音频流分区”。
- `storage_sd_benchmark_run()`：仅读写测试分支使用的内部诊断入口；成功挂载后由 `storage_sd_init()` 调用，不向其他任务公开。
- `storage_sdram_benchmark_run()`：本 Task 启动阶段的内部 SDRAM 硬件诊断与基准入口；不向其他任务公开。是否编译进调用由 `STORAGE_SDRAM_BENCHMARK_ENABLE` 门控；宏定义在 `APP/app_config.h`，但 `storage_task.c` 当前未包含该头。
- `storage_flash_benchmark_run()`：本 Task 启动阶段的 W25Q256 原始读取及可选双自检扇区破坏性基准入口；由 `storage_flash_init()` 在执行器绑定后调用，不向其他任务公开。

## 编译期依赖

- Platform SD 卡检测生命周期 Interface。
- Filesystem Service 的卷生命周期 Interface；文件 benchmark 另含卷感知文件 Interface；Flash 物理 benchmark 另含诊断 Interface。
- Log Service 投递 Interface。
- 仅用于主循环事件 FLAG、卡检测消抖和 DMA 完成的原生 FreeRTOS 索引任务通知 Interface。
- Platform SDRAM 的启动诊断 Interface。
- Platform Flash 的受限原始数组读取与固定双自检扇区诊断 Interface。

## 运行时请求与事件路径

卡检测 GPIO EXTI 经 Adapter、Platform SD 回调以 `eSetBits` 置本 Task `STORAGE_NOTIFY_EVENT` 的 `STORAGE_NOTIFY_FLAG_SD_DETECT`；GUI `request` 置同一槽的 `STORAGE_NOTIFY_FLAG_LISTBUFFER`。Filesystem 私有 DMA 执行器在同一 Task 上下文等待 `STORAGE_NOTIFY_SD_TRANSFER`。Filesystem Service Flash 私有执行器在 Task 启动时长期订阅 Platform Flash 的 QSPI IRQ，并以 `STORAGE_NOTIFY_FLASH_OPERATION` 等待 0xEC QSPI/MDMA 读取完成、`0x34/0x21` 自动状态轮询的匹配、错误或中止；任务醒来后才调用 Platform Flash 收尾。

`storage_task()` 主循环维护就绪 / 消抖中 / 未就绪，有界等待 `STORAGE_NOTIFY_EVENT`：检测 FLAG 立刻进消抖中并重开 30 ms 起点；`wait` 超时取消抖剩余与默认 100 ms 回收剩余的较小值。消抖截止到点后才 `storage_sd_process()`，再按 SD 是否已挂载回到就绪或未就绪。仅就绪时 `load`。回收截止到点才 `storage_flash_reclaim()`。FTL 决定是否回收，最长一次不可抢占擦除仍需板测。一次 Service 文件读写会在同一个 Task 的嵌套调用栈中进入 Filesystem 的 SD DMA 执行器，等待 `STORAGE_NOTIFY_SD_TRANSFER`、收尾当前分块后再启动下一分块；一次 Flash MDMA 读取则由 Service Flash 私有执行器在同一 Task 的嵌套调用栈中等待 `STORAGE_NOTIFY_FLASH_OPERATION`。ISR 只发布事件，绝不复制数据、维护 Cache 或启动下一笔传输。

## 音频流分区

`stream/` 把 SD 上的 MP3 以“音频块”交给播放任务（本分区只定义 Storage 一侧与调用契约，播放任务见 `APP/tasks/playback/`）。FIL 的打开、读取、关闭（含拔卡）都在 Storage Task（ARC-12）；播放任务只持流序号。

- 文件：`storage_audio_stream.*`（信箱、开启、读块、终止记录）、`storage_audio_block.*`（块池与两个队列）、`storage_audio_tag.*`（越过标签纯函数，主机测试见 `Tests/storage_audio_stream/`）、`storage_audio_config.h`（块长 32 KiB、块数 32，均为配置常量）。
- 请求信箱（播放任务 → Storage）：长度 1 的覆盖队列，内容 `{StreamSeq, Generation, Position}`，`StreamSeq` 为 0 表示无流；开启、关闭、取消是同一种写入，经 `storage_audio_stream_request()` 写入并置 `STORAGE_NOTIFY_FLAG_STREAM_REQUEST`。代次 0 表示“当前”。
- 流记录信箱（Storage → 播放任务）：长度 1 的覆盖队列，内容 `{StreamSeq, OpenResult, Generation, TrackCount, AudioBytes, End}`，整份覆盖，开启参数一直保留；播放任务用 `storage_audio_stream_get_record()` 非消费读取。开启结果：OK、NOT_READY、STALE、NO_TRACK、BAD_FILE（打不开、标签长度越界、音频区间为空）、IO_ERROR；终止：ENDED（只在最后一块入队之后写）、READ_ERROR、CARD_REMOVED。Service 无法区分“文件不存在”与“打开时 I/O 错误”，打开失败一律 BAD_FILE；位置超出列表按 STALE。BAD_FILE 时记录仍带 Generation 与 TrackCount（AudioBytes 为 0），其他失败三者皆 0。
- 块：`{StreamSeq, ValidBytes}` 加数据，有效字节数 1..块容量，任何块都可以是短块，不设 kind、末块标志或块序号。Storage 只用当前正在读的流序号盖章；读完后若请求信箱已有新请求，半读的块不入就绪队列，直接放回空闲队列。播放任务用 `storage_audio_block_take_ready()` / `peek_ready()` 取块，用 `storage_audio_stream_return_block()` 归还（放回空闲队列并置 `STORAGE_NOTIFY_FLAG_BLOCK_RETURNED`）。
- 唤醒播放任务：本分区不认识播放任务的事件槽，由播放任务外壳用 `storage_audio_stream_set_wake_callback()` 注册回调（`STORAGE_AUDIO_WAKE_STREAM_RECORD`、`STORAGE_AUDIO_WAKE_BLOCK_READY`），回调只能置 FLAG。FLAG 只用于唤醒，醒来后以信箱与队列内容为准。播放任务须在 `storage_task()` 完成 `storage_audio_stream_init()` 之后才能调用 `request`（此前返回 false）。
- 主循环每圈顺序：请求信箱 → 列表窗口 → 最多读一块 → 回收。还有工作（待处理请求，或流已打开且有空闲块）时不阻塞等待。结构上界：取消 ≤ 一次块读取；开启 ≤ f_open + 标签探测（读头、读尾、seek）+ 一次块读取。
- 有打开的流时（FIL 未关）不提供 Flash 回收机会，回收剩余不参与等待计时；流关闭（ENDED、读错、拔卡、无流请求）后照常，暂停期间因流保持而顺延。
- 消抖期：暂缓处理流请求和补块，不应答；已打开的流保持打开；消抖结束后卡仍在就继续，卡已拔则以 NOT_READY 或 CARD_REMOVED 收尾。
- 拔卡：`storage_sd_process()` 确认拔卡后，在作废曲库与卸载卷之前先 `storage_audio_stream_card_removed()`：关闭 FIL，再写 `{当前流序号, CARD_REMOVED}` 并唤醒播放任务；没有打开的流时不写、不唤醒。插卡事件同样先调用一次（换卡视同拔卡）。
- 块池内存与 HWD-2 审查：块池 32 × 32 KiB = 1 MiB 放 SDRAM NOLOAD 段，块内容入队才有效，不依赖清零，**块只由 CPU 读写，不是 DMA 目标**（SD DMA → bounce buffer → memcpy 进块 → 播放任务 memcpy 进窗口）。
  - 启动：`storage_audio_block_init()` 在 `storage_task()` 内、可选的 `storage_sdram_benchmark_run()` 全容量诊断之后、Flash/SD 初始化之前调用，只建队列并入队块指针，不触碰块内容；SDRAM 在 `main()` 经 `Platform/sdram` 完成正式初始化后任务才运行，满足“正式初始化之后、首次使用前”。队列控制块与存储放 DTCM `.bss`（约 416 B）。
  - MPU：`Core/Src/main.c` 的 Region 2 把 0xC0000000 起 32 MB 设为 Cacheable、Bufferable、不可执行、全访问，块池落其中；块只由单核 CPU 两个任务读写，无 DMA 与 Cache 一致性问题，ARC-18 不适用。
  - 全容量诊断：诊断是破坏性的，只能在块池入队前独占运行；`STORAGE_SDRAM_BENCHMARK_ENABLE` 开启时仍须保持在 `storage_audio_block_init()` 之前，产品默认关闭。
  - 链接段：块池在链接脚本 `STM32H743xG_flash.ld` 的独立段 `.storage_audio_pool` (NOLOAD, >SDRAM)，位于 `.sdram_framebuffer` 之后，不移动既有 SDRAM 段；段名由 `storage_audio_config.h` 的 `STORAGE_AUDIO_POOL_SECTION` 持有（用户授权，链接脚本由用户手改）。 `hw:pending`：上板确认 map 中块池落点、SDRAM 总用量在容量内。
- 板级行为（单圈耗时、消抖、拔卡、播放中不回收、1 MiB 缓冲下的断流余量）归板级验收票，Storage 音频流主循环不做主机测试。

## 约束

- `STORAGE_NOTIFY_EVENT` 属于本 Module：GPIO EXTI 置 `STORAGE_NOTIFY_FLAG_SD_DETECT`，主循环立刻进入消抖中；静默 30 ms 后才调用 `Platform_SD_Process()`。GUI 窗口请求置 `STORAGE_NOTIFY_FLAG_LISTBUFFER`，且仅 SD 已挂载时 `request` 成功。两 FLAG 不得拆成轮流阻塞的独立槽。SD 就绪由挂载事实决定，不由播放列表代次决定。
- `STORAGE_NOTIFY_SD_TRANSFER` 不由本任务主循环消费。它属于 Filesystem Service 的同步 SDMMC DMA 执行器；执行器在同一个 Storage Task 上下文、于 FatFs 读写期间等待它。
- `STORAGE_NOTIFY_FLASH_OPERATION` 属于 Filesystem Service Flash 执行器。它承载一个 QSPI 异步操作的完成、状态匹配、错误或中止；Flash 物理 benchmark 经 `Service_Filesystem_ReadFlashArray()` 等诊断入口等待该通知并触发 `Platform_Flash_ProcessOperation()` 判定结果。读取通知不能直接视为数据可用，写擦的状态匹配通知也不能直接视为 Device 已恢复 READY。
- APP 不注册 SDMMC 传输回调，不使用 `BSP_SD_*`、不调用 `HAL_SD_*`，也不访问 `hsd1` 或 DMA bounce buffer。
- 插卡只有在 Platform SD 报告 `READY` 后才挂载，成功后扫描曲库并生成顺序播放列表；拔卡先作废曲库与播放列表，再注销 FatFs 卷。设备不格式化 SD；未格式化只记录告警。
- 读写测试分支在首次成功挂载后经 Service 顺序写入 64 MiB、同步、顺序读取 64 MiB，再进行不计时完整性校验；所有日志使用 `SD: Bench ...`，校验成功后删除相对路径 `__sd_rw_bench.bin`，同一上电周期不重复执行。
- `storage_task_config.h` 保存卡检测消抖静默窗口和 Flash 回收周期；`storage_sd_benchmark_config.h` 保存仅 APP 诊断使用的测速数据规模。两者均不是 Filesystem Service 的 DMA 参数或对其他 Task 的公开 Interface。
- `storage_sdram_benchmark_config.h` 目前没有启用宏；`STORAGE_SDRAM_BENCHMARK_ENABLE` 在 `APP/app_config.h`。接入 SDRAM 业务数据后必须保持关闭，或在所有使用者前独占执行。
- `APP/app_config.h` 的 `STORAGE_FLASH_BENCHMARK_ENABLE` 决定是否在启动阶段运行 Flash 基准；`storage_flash_benchmark_config.h` 保存固定读取范围、4 KiB 工作缓冲、单块 MDMA 等待上限及破坏性自检开关。基准先后打印 1 MiB 的 `Bench poll read` 和 `Bench MDMA read`；两者都不改变 Flash 内容。`STORAGE_FLASH_BENCHMARK_PROGRAM_ENABLE` 开启时，Platform 只允许以首/尾区域语义擦写 ADR-0009 的两个 4 KiB 自检扇区：Storage Task 每页提交 `0x34` 后等待自动状态匹配，随后完成轮询读回逐字节校验，再使用 `STORAGE_NOTIFY_FLASH_OPERATION` 执行一次 MDMA 读回逐字节校验；两条路径都通过后仅输出一条自检成功日志，其中包含不计擦除和读回的 8 KiB 逐页编程端到端速率。最后基准以 4 KiB 间接参照交叉比对 `0x90000000` 映射窗口，并输出 1 MiB 的 `Bench memory-mapped read` 和 checksum；成功后映射保持开启，任何后续间接 Flash 操作都会由 Platform 暂时退出并在成功收尾后恢复。该入口只可在 Storage Task 启动早期独占执行。

## 命名

Task 内部 Implementation 使用 `storage_*`；任务入口保持 `storage_task()`；跨层调用使用 `Platform_SD_*`、`Service_Filesystem_*` 与 `Service_Log_*`。

## FTL 集成（已实现，待上板验收）

启动时先可选跑 SDRAM 破坏性自检，再由 `storage_flash_init()` 绑定执行器、按策略挂载已有卷。`MountFlash` 返回 `SERVICE_NO_FILESYSTEM` 时，是否调用 `FormatAndMountFlash` 由 `STORAGE_FLASH_AUTO_FORMAT` 控制（`storage_task_config.h`，当前默认开启）。物理基准在执行器绑定之后、挂载之前运行；文件基准仅在挂载成功后运行。QSPI 回调、`STORAGE_NOTIFY_FLASH_OPERATION` 等待和传输收尾职责属于 Filesystem Service 私有 Flash 执行器。Storage Task 仍是唯一上下文，APP 的 `flash/` 分区保留启动/诊断编排、挂载策略和回收调用时机，不注册第二个传输回调。随后 `storage_sd_init()` 处理卡槽热插拔。

主循环已为 `ReclaimFlash` 安排定期机会，不再无限期只等 SD 检测。FTL 决定是否 GC、回收哪个块；已在飞操作按硬件通知与超时推进。默认回收机会周期 100 ms，每次最多回收一块，不承诺 NOR 擦除可以立即抢占。SDRAM 破坏性自检必须早于 FTL 表与业务缓冲使用。

Flash 文件 benchmark 在成功挂载后运行：APP 通过 Service 完成新建、写入同步、
读回测速、校验和删除，不直接操作 FatFs 类型或 FTL 块。挂载失败则跳过文件基准；是否格式化由 `STORAGE_FLASH_AUTO_FORMAT` 在挂载策略中决定，不由 benchmark 触发。
测试文件仅在本轮成功创建后清理；同名文件保护、故障残留及配置见 benchmark/README.md。
