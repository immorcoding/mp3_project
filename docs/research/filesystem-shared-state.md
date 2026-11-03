# Filesystem 与 FTL 的共享状态和完成通知（#188）

- 票：`immorcoding/mp3_project#188`（wayfinder:research），地图 `#186`。
- 基线：分支 `research/filesystem-shared-state`，起点 `e38b39f`；`Service/filesystem/` 最近一次提交 2026-11-01。
- 方法：仓库代码为一手来源，引用写作 `路径:行号`；`build/` 下的 ELF 与 map 只读，用于栈估算。未运行主机测试，未做板测。`.vscode/` 下的 IDE 索引文件不作来源。
- 本文只记录事实。推断标注“推断”；读不到或未检索的标“未核实”。

## 结论摘要

1. Service 内没有任何锁或临界区（§1.5）；文件级可写状态是 13 个 `static` 变量（§1.1），无函数内 `static` 变量。
2. SD 与 Flash 执行器各把“唯一任务”固化为 owner 句柄，之后每次调用逐一比对（`Service/filesystem/sd/filesystem_sd_transfer.c:70-72,95-97,171-174`；`Service/filesystem/flash/filesystem_flash_transfer.c:39-43,57-59`）。
3. Flash 的 Service 入口检查 owner（`Service/filesystem/filesystem_handle.c:189-195`）；SD 的 Service 入口不检查（`filesystem_handle.c:196-204`），SD 的 owner 检查只在块读写执行器内（`sd/filesystem_sd_transfer.c:231-234`）。
4. FatFs 文件锁表 `Files[]` 只有 2 项，两卷共用（`FATFS/Target/ffconf.h:230`；`Middlewares/Third_Party/FatFs/src/ff.c:541,779-803`）；第三个不同文件的打开返回 `FR_TOO_MANY_OPEN_FILES`，被 `Service/filesystem/filesystem_status.c:51-52` 映射为 `SERVICE_ERROR`，与 README 所写的 BUSY 不同（`Service/filesystem/README.md:30`）。
5. FatFs 在磁盘状态为 `STA_NOINIT` 时清零已挂载卷的 `fs_type` 并尝试重挂（`ff.c:3021-3038`）；SD 的该状态取决于 Platform 是否 READY，DMA 在飞时为 BUSY（`Service/filesystem/sd/filesystem_sd_bsp.c:148-153`；`Components/sd/sd.c:637`）。这是代码路径推断，未测（§2.7）。
6. NOR 耗时：仓库无数据手册。代码注释引用 tPP≤4 ms、tSE≤400 ms、tW≤15 ms（`Components/w25qxx/w25qxx_config.h:48,49,58`），未对照手册原文。Service 写预算 120 s、格式化 3600 s，超时后仍轮询直至非 BUSY，无硬上限（`Service/filesystem/flash/filesystem_flash_config.h:14-19`；`flash/filesystem_flash_transfer.c:113-136`）。
7. 迟到的 SD DMA 完成：超时只把 Device 置 ERROR，不中止 DMA（仓库内无 `HAL_SD_Abort` 调用）；ERROR 且卡在位时 `SDCard_Refresh` 直接返回错误（`Components/sd/sd.c:390-393`）。迟到事件仍投递到 owner 的通知槽 1（`Adapters/stm32_hal/irq/stm32_sdmmc_irq.c:28-42`；`Service/filesystem/sd/filesystem_sd_transfer.c:43-61`）。节点注销后不再投递（`Platform/sd/platform_sd.c:188-196`）；挂起位在下次传输前清除（`Service/filesystem/sd/filesystem_sd_transfer.c:76-78`），清除之后到达的事件仍会被投递。通知值只含事件类型，无序号（`Service/filesystem/sd/filesystem_sd_transfer.c:55-59`）。
8. 栈：构建未启用 `-fstack-usage`，无 `.su`。Debug 构建（全部 `-O0`）的静态直接调用链从 `storage_task` 估得 2136 B；release 估得 1904 B。Storage Task 栈为 1024 word，即 4096 B（`APP/app_config.h:31`）。Service 单帧最大的是 518 B 的 TCHAR 路径缓冲（`Service/filesystem/filesystem_file.c:62`）和约 548 B 的 FILINFO（`filesystem_directory.c:238`，按成员推算）。
9. Service 公开函数共 32 个，分布在 5 个公开头（§5）；其中 19 个含多步或中间状态翻转。
10. 所有 Service 文件接口的调用方都在 `APP/tasks/storage/` 下（grep 命中 34 行，含声明，§2.5）；播放任务只经 `storage_audio_stream_*` 信箱接口（`APP/tasks/playback/playback_task.c:180,192,259`）。

---

## 1. 可写状态清单

### 1.1 `Service/filesystem/` 内的文件级可写状态（穷举）

| # | 位置 | 名称 | 类型与大小 | 作用域 | 写入方（行） | 读取方（行） |
|---|---|---|---|---|---|---|
| 1 | `Service/filesystem/filesystem_handle.c:35` | `filesystem_file_slots` | 4 项（`FILESYSTEM_FILE_SLOT_COUNT`，`filesystem_config.h:27`），每项为 `FIL` + `generation` + `volume` + `in_use` | 全局，SD 与 Flash 共用，按项内 `volume` 区分 | acquire L235-238、代次 L231、release L293-294、invalidate L110-111 | lookup L263-276、has_open L163-164 |
| 2 | `filesystem_handle.c:36` | `filesystem_directory_slots` | 4 项（`filesystem_config.h:28`），每项为 `DIR` + `generation` + `volume` + `in_use` | 全局，同上 | acquire L325-328、代次 L321、release L387-388、invalidate L119-120 | lookup L355-368、has_open L172-173 |
| 3 | `filesystem_handle.c:37` | `filesystem_volume_initialized` | `bool[2]`，下标为卷枚举 | 按卷 | `set_volume_initialized` L129；调用方只有 SD（`filesystem_service.c:56,62,66`），FLASH 下标从未写入 | `volume_ready` L196（仅 SD 分支） |
| 4 | `filesystem_handle.c:38` | `filesystem_volume_mounted` | `bool[2]` | 按卷 | `set_mounted` L140；SD 调用 `filesystem_service.c:110,112,129,134,139`，FLASH 调用 `:181,183,199,204,209` | `is_mounted` L149、`volume_ready` L201 |
| 5 | `Service/filesystem/filesystem_service.c:26` | `filesystem_mkfs_work_buffer` | `uint8_t[512]`（`filesystem_config.h:31`） | 全局，仅 Flash 格式化路径使用 | 作为 `f_mkfs` 工作区被 FatFs 写入（L239-243） | 同左 |
| 6 | `Service/filesystem/filesystem_directory.c:36` | `filesystem_directory_sector_buffer` | `BYTE[_MAX_SS]`，512 B | 全局，两卷的目录定位符采集共用 | `disk_read` L155 | L159-176 解析目录项；使用处 L267 |
| 7 | `Service/filesystem/sd/filesystem_sd_transfer.c:28` | `filesystem_sd_transfer_owner_task` | `TaskHandle_t` | SD 执行器（仅 SD） | `init` L166 | IRQ 回调 L49-50、L55；`prepare_wait` L70-71；`wait` L95-96；`init` L152、L171；`is_bound` L186 |
| 8 | `sd/filesystem_sd_transfer.c:29` | `filesystem_sd_notify_index` | `uint32_t` | SD | `init` L167 | IRQ 回调 L56；`wait` L101-102；`notify_index()` L195 |
| 9 | `sd/filesystem_sd_transfer.c:30` | `filesystem_sd_notify_bound` | `bool` | SD | `init` L168 | `is_bound` L186 |
| 10 | `sd/filesystem_sd_transfer.c:32-34` | `filesystem_sd_dma_buffer` | `uint8_t[512×64]`（32 KiB），按 `PLATFORM_DCACHE_LINE_SIZE` 对齐，段 `.sd_dma_buffer` | SD，全部 SD 块传输共用的 bounce | 写入侧 memcpy L291；DMA 写入经 Platform（L236、L297） | 读出侧 memcpy L249 |
| 11 | `flash/filesystem_flash_transfer.c:13` | `filesystem_flash_owner` | `TaskHandle_t` | Flash 执行器 | `init` L66 | `is_owner` L42；`init` 早退 L57-59；注册回调上下文 L62 |
| 12 | `flash/filesystem_flash_transfer.c:14` | `filesystem_flash_registered` | `bool` | Flash | `init` L68 | `is_owner` L41；`init` L57 |
| 13 | `flash/filesystem_flash_transfer.c:15` | `filesystem_flash_notify_index` | `uint32_t` | Flash | `init` L67 | ISR L27；`finish` 等待 L123 |

段落位置：第 10 项的段由 `STM32H743xG_flash.ld:323-328` 定义，落在 `RAM`（`STM32H743xG_flash.ld:59`，起点 `0x24000000`，即 AXI SRAM）。

### 1.2 Service 直接访问的下层可写全局（不在 `Service/filesystem/` 内）

| # | 位置 | 名称 | 作用域 | 写入方（行） | 读取方（行） |
|---|---|---|---|---|---|
| E1 | `FATFS/App/fatfs.c:16-22` | `retSD`、`SDPath[4]`、`SDFatFS`、`retUSER`、`USERPath[4]`、`USERFatFS` | 每卷一套（CubeMX 生成） | `MX_FATFS_Init` 的 `FATFS_LinkDriver` 调用（`fatfs.c:32,34`）；`f_mount(&SDFatFS/&USERFatFS)` 在 `filesystem_service.c:111,182` | `filesystem_service.c` 多处；`Service/filesystem/filesystem_path.c:198,202` |
| E2 | `Middlewares/Third_Party/FatFs/src/ff.c:533` | `FatFs[_VOLUMES]` 卷表 | 全局，两卷 | `find_volume` L3293；清零 L3284、L3288、L5421 | `find_volume` L3014 |
| E3 | `ff.c:534` | `Fsid` 挂载计数 | 全局 | L3192 `fs->id = ++Fsid` | `validate` L3224（对比 `obj->id`） |
| E4 | `ff.c:541`（容量 `FATFS/Target/ffconf.h:230` = 2） | `Files[_FS_LOCK]` 锁表 | 全局，跨卷 | `inc_lock` L825-827、L833；`dec_lock` L852-853；`clear_lock` L870 | `chk_lock` L779-793；`enq_lock` L802 |
| E5 | `ff.c:559`（`ffconf.h:112`，`_USE_LFN 1`） | `LfnBuf[256]`，512 B | 全局；每个卷的 `fs->lfnbuf` 都指向它（L3194） | 路径解析时写入 | 同左 |
| E6 | `FATFS/Target/sd_diskio.c:56` | `Stat`（`volatile`） | SD 驱动 | `SD_initialize` L109、L115；`SD_CheckStatus` L92、L96（由 `SD_status` L130-133 经 `disk_status` 调用） | `SD_ioctl` L213 |
| E7 | `FATFS/Target/user_diskio.c:45` | `Stat`（`volatile`） | USER 驱动 | `USER_initialize` L85-86；`USER_status` L100-101 | 无读取 |
| E8 | `Middlewares/Third_Party/FatFs/src/ff_gen_drv.c:24` | `disk`（驱动表，非 `const`） | 全局 | 驱动注册 `FATFS_LinkDriverEx`（`ff_gen_drv.c:39`）；`disk_initialize` 置 `is_initialized`（`diskio.c:62`） | `diskio.c:42,59,85,107,128` |
| E9 | `ff.h:85-126`（`FATFS.win[_MAX_SS]`，L125）；`ff.h:155-172`（`FIL.buf[_MAX_SS]`，L170） | 每卷的磁盘窗口；每个 `FIL` 的数据窗口 | `FATFS` 按卷；`FIL` 在 E 槽内 | FatFs 内部 | FatFs 内部 |

Platform 与 APP 中的静态状态见 §2.5 与 §3.1。

### 1.3 非静态（栈上，每次调用独立，不构成共享状态）

- TCHAR 路径缓冲 `TCHAR native_path[FILESYSTEM_TCHAR_PATH_LENGTH]`，长度 255+4=259 个 TCHAR，约 518 B（`filesystem_config.h:20`）。位置：`filesystem_file.c:62`（OpenFile）、`:312`（RemoveFile）、`:355-356`（RenameFile，两个）、`:389`（GetFileInfo）；`filesystem_directory.c:192`（OpenDirectory）、`:329`（CreateDirectory）、`:355`（RemoveDirectory）。
- 驱动盘符缓冲 `drive_path[4]`：`filesystem_path.c:213`、`Service/filesystem/filesystem_file_locator.c:32`。
- FILINFO：`filesystem_directory.c:238`（ReadDirectory）、`filesystem_file.c:313`（RemoveFile）、`filesystem_directory.c:356`（RemoveDirectory）、`filesystem_file.c:390`（GetFileInfo）。

### 1.4 句柄代次与 Token

- 代次字段：`filesystem_handle.c:22,30`；上限 `0x00FFFFFF`（`filesystem_handle.c:16`）；Token = `(代次 << 8) | (槽号+1)`（`filesystem_handle.c:57-60`）。
- 代次在每次 acquire 时先递增再使用（`filesystem_handle.c:231`、`:321`）；卸载失效时不清代次（`filesystem_handle.c:109-111`、`:120`），因此旧 Token 不会命中新对象。
- 代次耗尽时，acquire 在第一个空闲槽上直接返回 `SERVICE_ERROR`，不尝试后续槽（`filesystem_handle.c:229-234`）。

### 1.5 搜索核对

1. `Grep` 模式 `\bstatic\b`，路径 `Service/filesystem`，内容输出：命中 40 行。其中 13 行为上表变量（`filesystem_handle.c:35-38`、`filesystem_service.c:26`、`filesystem_directory.c:36`、`sd/filesystem_sd_transfer.c:28-30`、`:32`、`flash/filesystem_flash_transfer.c:13-15`），其余 27 行为 `static` 函数定义或 `static inline`（`filesystem_locator.h:31,53`）。全部命中行首无缩进，因此无函数内 `static` 变量。
2. `Grep` 模式 `^[A-Za-z_][^(){}]*;`（无括号的分号行，覆盖非 `static` 文件级变量）：命中 12 行，全部为上表变量；多行声明 `sd/filesystem_sd_transfer.c:32-34` 由第 1 条补齐。
3. `Grep` 模式 `\bextern\b`：在 `Service/filesystem` 内无 `extern` 声明（仅 `SDPath`、`SDFatFS` 等使用点）。
4. `Grep` 锁与临界区原语 `xSemaphore|Mutex|portENTER_CRITICAL|taskENTER_CRITICAL|taskDISABLE_INTERRUPTS|vTaskSuspendAll|__disable_irq|configASSERT`：命中 0。仅有 `xTaskNotify*` 与 `TaskHandle_t`。
5. 全仓检索 `set_volume_initialized|set_mounted|sd_transfer_init|flash_transfer_init`：调用方只在 `filesystem_service.c`；`Tests/` 中有同名桩（§2.6）。

---

## 2. 任务身份绑定

### 2.1 调用者身份被固定的位置

| 位置 | 作用 | 判定结果 |
|---|---|---|
| `Service/filesystem/sd/filesystem_sd_transfer.c:164-169` | 首次 `InitSD` 绑定 owner 与通知槽 | 之后 `owner != 当前任务` 返回 false（L171-174） |
| `sd/filesystem_sd_transfer.c:70-72`、`:95-97` | `prepare_wait` 与 `wait` 检查当前任务 | 非 owner 返回 false，块读写失败 |
| `sd/filesystem_sd_transfer.c:49-51` | IRQ 回调校验 `context` 是 owner 变量的地址 | 不一致则直接返回 |
| `Service/filesystem/flash/filesystem_flash_transfer.c:39-43` | `is_owner()`：已注册且当前任务等于 owner | 各操作入口检查（L95、L172、L216、L238、L258、L273、L288、L304） |
| `flash/filesystem_flash_transfer.c:51-70` | `init`：首次注册；已注册时只有同一 owner 返回 true（L57-59） | 其他任务返回 false |
| `Service/filesystem/flash/filesystem_flash_bsp.c:27-33` | `BSP_USER_DISKIO_GetStatus`：仅 owner 且卷 READY 返回 0，否则 `STA_NOINIT` | FatFs 经 `disk_status` 看到 NOINIT（`ff.c:3022,3236`） |
| `Service/filesystem/filesystem_handle.c:189-195` | `volume_ready(FLASH)` 调用 `is_owner` | 非 owner 返回 `SERVICE_NOT_READY` |
| `filesystem_handle.c:196-204` | `volume_ready(SD)` 只查 initialized 与 mounted | 无 owner 检查 |
| `Service/filesystem/filesystem_service.c:36`、`:197` | Flash 挂载与卸载前检查 owner | 非 owner 返回 NOT_READY |
| `filesystem_service.c:75-83` | `ensure_bound` 调 `InitSD`，经 `transfer_init` 比对 owner | 非 owner 时 InitSD 失败，挂载返回 NOT_READY（L101-103） |

### 2.2 通知槽

| 槽 | 数值 | 持有方 | 注入位置 | 来源 |
|---|---|---|---|---|
| `STORAGE_NOTIFY_EVENT` | 0 | APP 主循环 | 无注入；Storage 主循环等待 `APP/tasks/storage/storage_task.c:177-181` | `APP/tasks/storage/storage_task.h:26` |
| `STORAGE_NOTIFY_SD_TRANSFER` | 1 | Service SD 执行器 | `APP/tasks/storage/sd/storage_sd.c:103` 调用 `Service_Filesystem_InitSD` | `storage_task.h:27` |
| `STORAGE_NOTIFY_FLASH_OPERATION` | 2 | Service Flash 执行器 | `APP/tasks/storage/flash/storage_flash.c:61` 调用 `Service_Filesystem_InitFlash` | `storage_task.h:28` |

- 槽数上限：`Middlewares/Third_Party/FreeRTOS/Config/FreeRTOSConfig.h:30` 为 4；`APP/tasks/storage/storage_task.c:257-259` 有 `_Static_assert(STORAGE_NOTIFY_COUNT ≤ 4)`。
- 写入方式：SD 用 `eSetValueWithOverwrite`（`sd/filesystem_sd_transfer.c:55-59`）；Flash 用同样方式（`flash/filesystem_flash_transfer.c:26-30`）。
- 卡检测用槽 0：ISR 以 `eSetBits` 置 `STORAGE_NOTIFY_FLAG_SD_DETECT`（`APP/tasks/storage/sd/storage_sd.c:196-208`）。Service 文档称卡检测槽“只由 Storage Task 自己等待，不进入 Service”（`Service/filesystem/README.md:56`）。

### 2.3 Platform 回调

| 回调 | 注册位置 | 订阅数 | 忙时约束 | ISR 上下文 |
|---|---|---|---|---|
| SD 卡检测 | `APP/tasks/storage/sd/storage_sd.c:242-243` 调用 `Platform_SD_Init(storage_sd_detect_callback, task_handle)` | Init 时一个 | 不可重复 Init（`Platform/sd/platform_sd.c:211`） | EXTI（`platform_sd.c:106-116`） |
| SD DMA 完成 | `Platform/sd/platform_sd.c:321-348` 的 `SetTransferCallback`，由 `sd/filesystem_sd_transfer.c:176-177` 调用 | 单订阅者 | DMA 在飞（BUSY）时拒绝替换（`platform_sd.c:330`） | SDMMC IRQ（`platform_sd.c:125-157`） |
| Flash 异步操作 | `Platform/flash/platform_flash.c:634-648` 的 `SetOperationCallback`，由 `flash/filesystem_flash_transfer.c:61-62` 调用 | 单订阅者 | 操作在飞时拒绝（`platform_flash.c:639`） | QSPI IRQ，且仅在操作在飞时转发（`platform_flash.c:158-163`） |
| Flash 清除 | `platform_flash.c:658-668` 的 `ClearOperationCallback` | — | 在飞时拒绝（`platform_flash.c:660`） | 调用者未检索，未核实 |

### 2.4 APP 句柄检查

- `APP/tasks/storage/flash/storage_flash.c:33-36`：`storage_flash_is_storage_task` 要求句柄非空且等于当前任务；`storage_flash_init` 在 L51-57 不满足时返回 `STORAGE_ERROR`。
- `storage_flash.c:59` 保存 `storage_flash_owner_task`，随后 L61 注入通知槽 2。
- `storage_flash_reclaim`（`storage_flash.c:120-130`）用保存的句柄与当前任务比较（L124）。
- Service 侧的 owner 在 `flash/filesystem_flash_transfer.c:61` 取 `xTaskGetCurrentTaskHandle()`，与 APP 保存的句柄在同一调用路径上相同。

### 2.5 APP 静态变量持有的 Service 句柄

- `APP/tasks/storage/catalog/storage_catalog.c:35`：`music_directory`（DIR Token）。
- `storage_catalog.c:50`：`MusicCatalogLocator[]`（定位符数组）。
- `APP/tasks/storage/stream/storage_audio_stream.c:39`：`storage_audio_file`（文件 Token）。
- Service 文件接口的 APP 调用点（grep 命中行，含声明）：`storage_catalog.c` 8、`storage_track_open.c` 4、`storage_track_text.c` 8、`storage_flash.c` 4、`storage_sd.c` 3、`storage_audio_stream.c` 7，合计 34，全部在 `APP/tasks/storage/` 内。
- 播放任务：`APP/tasks/playback/playback_task.c:180,192,259,438,443` 只调用 `storage_audio_stream_*`，未直接调用 Service 文件接口（检索范围：`APP`、`Service`、`Components`、`Platform`）。

### 2.6 文档声明与测试桩

- `Service/filesystem/README.md:34`：所有函数均在唯一 Storage Task 同步执行；首版没有跨任务文件请求队列。
- `Service/filesystem/README.md:64`：Flash 执行器拒绝其他任务。
- 主机测试以桩替换执行器初始化：`Tests/flash_ftl/test_filesystem_service.c:123,139`、`Tests/flash_ftl/test_flash_file_benchmark.c:27,43`、`Tests/filesystem_locator/test_filesystem_locator.c:47,76`。这些桩替换了执行器的 `*_transfer_init` 本身，因此 §2.1 的 owner 比对不在这些主机测试的覆盖之内（代码事实；未运行测试）。

### 2.7 SD 入口无 owner 检查时的代码路径（推断，未测）

1. 非 owner 任务调用 SD 路径类 FatFs 接口（如 `f_stat`）→ `find_volume`（`ff.c:3014-3022`）调用 `disk_status`（`Middlewares/Third_Party/FatFs/src/diskio.c:36-44`）→ `SD_status`（`FATFS/Target/sd_diskio.c:130-133`）→ `SD_CheckStatus`（`sd_diskio.c:90-100`）。
2. `BSP_SD_GetCardState()` 取 Platform 状态（`Service/filesystem/sd/filesystem_sd_bsp.c:148-153`）。owner 的 DMA 在飞时 Device 为 BUSY（`Components/sd/sd.c:637`；`Platform/sd/platform_sd.c:77-97`），返回非 OK，于是 `Stat = STA_NOINIT`（`sd_diskio.c:92-97`）。
3. 已挂载分支不返回（`ff.c:3023-3029`），进入重挂分支：`fs->fs_type = 0`（`ff.c:3034`），`disk_initialize` 失败则返回 `FR_NOT_READY`（`ff.c:3036-3038`）。
4. 之后 owner 的文件对象在 `validate` 中因 `fs_type == 0` 失败（`ff.c:3224`）；`filesystem_volume_mounted[SD]` 仍为真，因为没有经过 `set_mounted`（`filesystem_handle.c:133-145`）。

以上为代码路径推断，未在主机或板上验证。

---

## 3. FTL 与 Flash

### 3.1 单一调用者的实现方式

- Platform 全局闸：`hplatform_flash_operation_active`（`Platform/flash/platform_flash.c:83`）。异步读启动（`platform_flash.c:697-700`）与卷操作启动（`platform_flash.c:1107-1110`）在它为真时返回 BUSY。`Platform_Flash_ProcessOperation` 在无在飞请求时返回 ERROR（`platform_flash.c:744-747`）。
- Service 闸：`flash/filesystem_flash_transfer.c:95`（finish）与各操作入口的 `is_owner`（L172、L216、L238、L258、L273、L288、L304）。
- 文档：FTL 为“单个普通执行上下文、同时一笔操作”（`Components/flash_ftl/README.md:20`）；Platform 注释写明 `ProcessOperation` “仅唯一普通执行上下文调用”（`platform_flash.c:740`），卷操作注释同样（`platform_flash.c:1193`、`:1209`、`:1224`）。
- 这些闸以“在飞标志”区分并发，不以任务身份区分；任务身份只在 Service 层判定（`flash/filesystem_flash_transfer.c:39-43`）。

### 3.2 Quiesce 与 QSPI 映射开关的时序

1. 开启映射的条件（`platform_flash.c:291-343`）：QSPI IRQ 已注册（L297）；无操作在飞（L302）；未处于映射（L307）；读取 SR1/SR2 且 WIP=0（L312-321）；取得读协议（L323）；适配器 `EnableMemoryMappedMode`（L328）；置标志（L341）。
2. 每次间接操作开始前先退出映射（`platform_flash.c:376-391`，由 `StartReadArray` L702 与 `start_volume` L1118 调用），并记录完成后是否恢复（L708-710、L1153-1154）。
3. 成功收尾后恢复（`platform_flash.c:780-794`）：调用 `enable_internal`，其中再次读取 WIP（L312-321）。若 BUSY，则 `ftl_result = IO_ERROR` 且卷入口关闭（L788-793）。失败分支不恢复（恢复只在 L786 的 OK 分支）。
4. Platform 侧的 Quiesce：失败时保持操作在飞，返回 BUSY（`platform_flash.c:774-777`）。
5. 适配器 Quiesce（`Adapters/stm32_hal/w25qxx_qspi/w25qxx_qspi_stm32_hal_adapter.c:780-820`）：关闭 QSPI 中断与 DMA 请求（L788-789）；`HAL_MDMA_Abort`（L792）；`HAL_QSPI_Abort`（L797）；BUSY 标志仍在则置 `QUADSPI_CR_ABORT` 并返回 BUSY（L801-805）；清除标志，收尾 DMA 读的 Cache 失效（L811）；`MemoryMappedModeEnabled = false`（L817）。注释写明不向 NOR 发复位，也不取消芯片内部擦写（L778）。
6. 组件层 `W25Qxx_Quiesce`（`Components/w25qxx/w25qxx.c:1246-1261`）：调用 BusOps 的 Quiesce，**无条件**把 `State` 置为 `W25QXX_STATE_ERROR`（L1253），成功才清除 ActiveOperation（L1258）。
7. 恢复：`Platform_Flash_RecoverVolume`（`platform_flash.c:1375-1396`）依次关闭映射（L1381）、Quiesce（L1385）、`W25Qxx_Recover`（L1390，检查 WIP 与 QE）。
8. Abort：`Platform_Flash_AbortOperation`（`platform_flash.c:1350-1365`）取消之后的映射恢复（L1356），FTL 进入 Abort（L1359）或标记 raw_failed（L1363）。

### 3.3 映射区的读取者

- 仓库内 `Platform_Flash_EnableMemoryMappedMode` 有两个调用点。一是资源服务（`Service/resource/resource_service.c:547`）：`Service_Resource_Init()` 在 `app_init()` 里、调度器启动前运行（`APP/app.c:138`），把资源复制进 SDRAM，不向其他 Module 发布 NOR 指针（`Service/resource/README.md:31`）。二是启动基准（`APP/tasks/storage/benchmark/storage_flash_benchmark.c:200`），由 `STORAGE_FLASH_BENCHMARK_ENABLE` 控制（`APP/tasks/storage/storage_flash.c:69-71`），产品默认 0（`APP/app_config.h:23-24`）。
- 基准文档写明“成功后映射保持开启，供后续 Resource Pack 使用”（`APP/tasks/storage/benchmark/README.md:11`）。运行中未见其他读取者。
- 映射窗口：`0x90000000`，32 MiB（`Platform/flash/platform_flash_config.h:24-25`）。
- 检索：在 `Platform`、`Adapters`、`APP`、`Components`、`Service` 中搜索 `EnableMemoryMappedMode|MemoryMapped|0x90000000`，除 benchmark、资源服务、适配器与 Platform 本身外无命中；CMSIS 头文件中的定义不计。（初版漏掉了资源服务，主会话核对后更正。）

### 3.4 时序与预算

| 量 | 数值 | 来源 | 性质 |
|---|---|---|---|
| 物理块（一组版本独占） | 4096 B | `Components/flash_ftl/flash_ftl_config.h:13`；`docs/shape/sources/storage-format.md`（几何行） | 格式事实 |
| 组 | 7 扇区，载荷 3584 B | `flash_ftl_config.h:15,34` | 格式事实 |
| 页 | 256 B；块内头 1 页、数据 14 页、提交 1 页 | `flash_ftl_config.h:14`；`storage-format.md`（几何行） | 格式事实 |
| 页编程软件时限 | 5 ms（注释引用 tPP≤4 ms） | `Components/w25qxx/w25qxx_config.h:48` | 软件时限；注释引用 |
| 扇区擦除软件时限 | 500 ms（注释引用 tSE≤400 ms） | `w25qxx_config.h:49` | 同上 |
| SR2 写入软件时限 | 20 ms（注释引用 tW≤15 ms） | `w25qxx_config.h:58` | 同上 |
| 数组读（MDMA 等待） | 100 ms | `w25qxx_config.h:47` | 软件时限 |
| Service 写请求预算 | 120000 ms | `Service/filesystem/flash/filesystem_flash_config.h:15` | 预算，超时即 Abort |
| Service 格式化预算 | 3600000 ms（注释：24 MiB 全擦约六千次） | `filesystem_flash_config.h:17` | 预算 |
| Service 回收预算（一块） | 2000 ms | `filesystem_flash_config.h:19` | 预算 |
| Service 打开预算 | 120000 ms | `filesystem_flash_config.h:16` | 预算 |
| Service 读预算 | 30000 ms | `filesystem_flash_config.h:14` | 预算 |
| Service 恢复预算 | 2000 ms | `filesystem_flash_config.h:18` | 预算 |
| 超时后的行为 | Abort 一次，之后继续 Process 直至非 BUSY | `flash/filesystem_flash_transfer.c:113-118`、`:135-136` | 无硬上限，取决于 Platform、FTL、W25Qxx 的终止 |

- 一组版本的写入（推断）：至少 16 次页编程（头 1 + 数据 14 + 提交 1）。若需擦除块，再加 1 次擦除；仓库中“是否内联擦除”取决于块池，未核实。
- 按注释引用的手册上限估算（推断）：无擦除 16×4 ms ≈ 64 ms；含擦除 ≈ 64 ms + 400 ms ≈ 464 ms。按软件时限（5 ms、500 ms）估算约 80 ms 与 580 ms。
- 回收（推断）：一次最多一块（`Components/flash_ftl/README.md:26`；`docs/shape/storage.execution.md:9` STOR-12），对应一次擦除，上限约 400 ms（注释引用）或 500 ms（软件时限）。
- 仓库中没有数据手册文件（检索 `*.pdf` 命中 0）。以上数值的手册原文未核实。

---

## 4. 迟到的 DMA 完成事件

### 4.1 超时后的现状路径（按顺序）

1. 等待超时：`filesystem_sd_transfer_wait` 返回 false（`sd/filesystem_sd_transfer.c:101-107`）。每个 DMA 分块的预算为 `FILESYSTEM_FATFS_BSP_DMA_TIMEOUT_MS` = 500 ms（`Service/filesystem/filesystem_config.h:42`）。
2. `finish` 以 ERROR 调用 `Platform_SD_CompleteTransfer`（`sd/filesystem_sd_transfer.c:127-130`）→ `Platform/sd/platform_sd.c:584-588` → `SDCard_FailTransfer`（`Components/sd/sd.c:797-816`）。卡在位时直接置 `SDCARD_STATE_ERROR`，不检查 BUSY 前置条件。
3. DMA 未中止：仓库中无 `HAL_SD_Abort` 调用（检索 `HAL_SD_Abort|HAL_SD_AbortCallback`，仅 `FATFS/Target/bsp_driver_sd.c:231-234` 的回调定义；`BSP_SD_AbortCallback` 为空的弱函数，`bsp_driver_sd.c:262-265`）。
4. 迟到的完成事件路径：HAL 完成回调 → `stm32_sdmmc_irq_read_complete`（`Adapters/stm32_hal/irq/stm32_sdmmc_irq.c:45-48`，回调在 `:76-113` 安装）→ `stm32_sdmmc_irq_dispatch`（`:28-42`）遍历节点 → `platform_sd_transfer_irq_cb`（`platform_sd.c:125-157`）→ `filesystem_sd_transfer_irq_callback`（`sd/filesystem_sd_transfer.c:43-61`）→ `xTaskNotifyIndexedFromISR(owner, 1, event, eSetValueWithOverwrite)`。
5. 节点注册状态：`Platform_SD_Refresh` 只在 `SDCard_Refresh` 成功时才同步节点（`platform_sd.c:301-307`），而 ERROR 下 `SDCard_Refresh` 直接返回错误（`sd.c:390-393`），因此节点保持注册（推断）。`SetTransferCallback` 在非 READY 时注销节点（`platform_sd.c:188-196`，经 `:340` 调用）；该调用由 InitSD 路径发出（`sd/filesystem_sd_transfer.c:176-177`）。
6. 挂起位清除：下一次 `prepare_wait` 调用 `xTaskNotifyStateClearIndexed`（`sd/filesystem_sd_transfer.c:76-78`），清除挂起状态而不清除数值（FreeRTOS 声明与说明见 `Middlewares/Third_Party/FreeRTOS/Source/include/task.h:3211-3243`）。
7. 新传输的前提：`Platform_SD_StartReadBlocks` 要求节点已注册（`platform_sd.c:537-540`）；`SDCard_StartReadBlocks` 要求 State 为 READY（`sd.c:620-626`）。ERROR 下无法启动新传输。回到 READY 的路径是卡拔出（`sd.c:362-381` 进入 NOT_PRESENT 并 DeInit 端口）后再插入（`sd.c:384-388` → `SDCard_Init`；适配器 `sd_stm32_hal_init` 调用 `HAL_SD_Init`，`Adapters/stm32_hal/sd/sd_stm32_hal_adapter.c:165`），并由 Platform 重新注册节点（`platform_sd.c:303-307`）。
8. 完成判定：`finish` 只比较事件类型（`sd/filesystem_sd_transfer.c:133-139`）。通过后 `Platform_SD_CompleteTransfer` → `SDCard_CompleteTransfer` 要求 State 为 BUSY（`sd.c:772-777`），再经 `sdcard_wait_ready` 调用 `Sync`，超时 1000 ms（`sd.c:780`；`Components/sd/sd_config.h:17`）。适配器的 `Sync` 轮询 `HAL_SD_GetCardState` 直至 TRANSFER（`sd_stm32_hal_adapter.c:508-520`）。

### 4.2 与 `docs/shape/storage.md:16` 的对照

- 一致之处：超时仅置 ERROR、不中止 DMA、通知值无传输序号、`prepare_wait` 清挂起位（对应 §4.1 第 2、3、4、6 步）。
- 代码另有的事实：ERROR 下 `Refresh` 不恢复（第 5 步）；新传输需要经过卡拔插与 HAL 重新初始化（第 7 步）。因此 open question 所说的残余窗口，在代码中只能出现在 HAL 重初始化之后；HAL 重初始化是否停止在途 IDMA 与中断，未核实（HAL 源码未读，未板测）。
- 完成判定以卡状态 TRANSFER 为准（`sd_stm32_hal_adapter.c:508-520`）；卡状态与 DMA 数据阶段完成的对应关系未核实。

### 4.3 等待者不再固定时的变化（事实与推断）

- 现状：ISR 读取 `filesystem_sd_transfer_owner_task`（`sd/filesystem_sd_transfer.c:55`），该变量只在首次 InitSD 设置（`:164-169`）；检索 `filesystem_sd_transfer_owner_task\s*=`，仅 L166 一处写入。
- 推断：若等待者改为多个任务，迟到事件投递到的任务与槽号由 ISR 时刻的 owner 决定；通知值没有任务或传输标识，接收方只能比较事件类型。
- Flash 侧：通知只是唤醒提示，结果由 `Platform_Flash_ProcessOperation` 判定（`flash/filesystem_flash_transfer.c:122`；`platform_flash.c:742-796`）。Flash 路径不以通知值作为完成依据。

---

## 5. 公开函数清单

“多步”指函数内有两次以上 FatFs 或执行器调用，或有句柄占用/释放、挂载标志翻转等中间状态。

### 5.1 `filesystem_service.h`（9 个，`Service/filesystem/filesystem_service.c`）

| 函数 | 定义 | 多步 | 步骤与行 |
|---|---|---|---|
| `Service_Filesystem_InitSD` | :52 | 多步 | 检查 `retSD`/`SDPath` L54 → `filesystem_sd_transfer_init` L60 → 置初始化标志 L66 |
| `Service_Filesystem_MountSD` | :92 | 多步 | 检查开放对象 L96 → `ensure_bound` L101 → 标志置假 L110 → `f_mount` L111 → 标志按结果置位 L112 |
| `Service_Filesystem_UnmountSD` | :122 | 多步 | `ensure_bound` L127 → `f_mount(NULL)` L138 → 标志置假 L139 |
| `Service_Filesystem_InitFlash` | :150 | 单步 | 执行器注册（`flash/filesystem_flash_transfer.c:51-70`） |
| `Service_Filesystem_MountFlash` | :161 | 多步 | 检查开放对象 L166 → 执行器打开（FTL 扫描）L171 → 标志置假 L181 → `f_mount` L182 → 标志置位 L183 |
| `Service_Filesystem_UnmountFlash` | :192 | 多步 | owner 与路径检查 L197 → `f_mount(NULL)` L208 → 标志置假 L209 |
| `Service_Filesystem_FormatAndMountFlash` | :219 | 多步（4 段） | `UnmountFlash` L222 → FTL 格式化 L233 → `f_mkfs` L239 → `MountFlash` L249 |
| `Service_Filesystem_ReclaimFlash` | :257 | 单步 | 执行器回收（`flash/filesystem_flash_transfer.c:286-295`） |
| `Service_Filesystem_RecoverAndMountFlash` | :267 | 多步（3 段） | `UnmountFlash` L269 → 恢复循环 L276 → `MountFlash` L282（含 FTL 重扫，`flash/filesystem_flash_transfer.c:328-329`） |

### 5.2 `filesystem_file.h`（12 个，含 `OpenFileByLocator` 的实现在 `filesystem_file_locator.c:26`）

| 函数 | 定义 | 多步 | 步骤与行 |
|---|---|---|---|
| `Service_Filesystem_OpenFile` | `filesystem_file.c:57` | 多步 | 路径转换 L73 → 占槽 L79 → `f_open` L85 → 失败释放 L88 → APPEND 时 `f_lseek` L94，失败则 `f_close` 与释放 L97-98 |
| `Service_Filesystem_OpenFileByLocator` | `filesystem_file_locator.c:26` | 多步 | 根路径 L50 → 占槽 L56 → 定位符非空检查 L62（空则释放）→ `f_open_dirent` L68 → 失败释放 L81 |
| `Service_Filesystem_ReadFile` | `filesystem_file.c:116` | 单次 FatFs | 查表 L135 → `f_read` L141（FatFs 内部可多次 `disk_read`） |
| `Service_Filesystem_WriteFile` | `filesystem_file.c:155` | 单次 FatFs | 查表 L174 → `f_write` L180 |
| `Service_Filesystem_SeekFile` | `filesystem_file.c:192` | 单次 FatFs | 查表 L196 → 偏移检查 L202 → `f_lseek` L207 |
| `Service_Filesystem_GetFilePosition` | `filesystem_file.c:216` | 无 FatFs 调用 | 查表 L227，读 `f_tell` 宏 L233 |
| `Service_Filesystem_GetFileSize` | `filesystem_file.c:243` | 无 FatFs 调用 | 查表 L254，读 `f_size` 宏 L260 |
| `Service_Filesystem_SyncFile` | `filesystem_file.c:270` | 单次 FatFs | 查表 L273 → `f_sync` L275 |
| `Service_Filesystem_CloseFile` | `filesystem_file.c:284` | 多步 | 查表 L287 → `f_close` L294 → 成功后释放槽 L297（失败保留槽，见注释 L280） |
| `Service_Filesystem_RemoveFile` | `filesystem_file.c:309` | 多步 | volume 就绪 L314 → 路径 L321 → `f_stat` L327 → 目录检查 L336 → `f_unlink` L341（两次 FatFs 调用之间无保护） |
| `Service_Filesystem_RenameFile` | `filesystem_file.c:351` | 单次 FatFs | volume 就绪 L357 → 两个路径 L364、L369 → `f_rename` L375 |
| `Service_Filesystem_GetFileInfo` | `filesystem_file.c:385` | 单次 FatFs | volume 就绪 L398 → 路径 L404 → `f_stat` L410 → 拷贝 L416-420 |

### 5.3 `filesystem_directory.h`（6 个，`Service/filesystem/filesystem_directory.c`）

| 函数 | 定义 | 多步 | 步骤与行 |
|---|---|---|---|
| `Service_Filesystem_OpenDirectory` | :187 | 多步 | 路径 L202 → 占槽 L208 → `f_opendir` L214 → 失败释放 L217 |
| `Service_Filesystem_ReadDirectory` | :233 | 多步 | 查表 L246 → `f_readdir` L254 → 名称转换 L263 → 定位符采集 L267（可能 `disk_read` 到静态扇区缓冲，L155） |
| `Service_Filesystem_RewindDirectory` | :282 | 单次 FatFs | 查表 L286 → `f_readdir(NULL)` L293 |
| `Service_Filesystem_CloseDirectory` | :301 | 多步 | 查表 L305 → `f_closedir` L312 → 释放 L315 |
| `Service_Filesystem_CreateDirectory` | :326 | 单次 FatFs | volume 就绪 L330 → 路径 L337 → `f_mkdir` L343 |
| `Service_Filesystem_RemoveDirectory` | :352 | 多步 | volume 就绪 L357 → 路径 L364 → `f_stat` L370 → 目录检查 L375 → `f_unlink` L380（`FR_DENIED` 映射为 BUSY，L381-384） |

### 5.4 `filesystem_text.h`（1 个）

| 函数 | 定义 | 多步 | 说明 |
|---|---|---|---|
| `Service_Filesystem_GbkToUnicode` | `filesystem_text.c:17` | 单步 | `ff_convert`，注释写明无介质访问、不阻塞（`filesystem_text.c:14-15`） |

### 5.5 `filesystem_flash_access.h`（4 个，`Service/filesystem/flash/filesystem_flash_access.c`）

| 函数 | 定义 | 多步 | 说明 |
|---|---|---|---|
| `Service_Filesystem_ReadFlashArray` | `flash/filesystem_flash_access.c:19` | 多步（内部） | owner 检查 L24 → `Platform_Flash_StartReadArray` L28-29 → `finish` 轮询（`flash/filesystem_flash_transfer.c:92-137`） |
| `Service_Filesystem_ReadFlashDiagnostic` | `:41` | 多步（内部） | owner 检查 L47 → `Platform_Flash_StartDiagnosticRead` L51-52 → `finish` |
| `Service_Filesystem_EraseFlashDiagnostic` | `:62` | 多步（内部） | owner 检查 L65 → `Platform_Flash_StartDiagnosticErase` L69 → `finish` |
| `Service_Filesystem_ProgramFlashDiagnostic` | `:83` | 多步（内部） | owner 检查 L90 → `Platform_Flash_StartDiagnosticPageProgram` L94-95 → `finish` |

统计：含多步或中间状态翻转的函数为 19 个（§5.1 的 7 个，§5.2 的 4 个 `OpenFile`、`OpenFileByLocator`、`CloseFile`、`RemoveFile`，§5.3 的 4 个 `OpenDirectory`、`ReadDirectory`、`CloseDirectory`、`RemoveDirectory`，§5.5 的 4 个诊断入口）。其余为单步或单次 FatFs 调用加前置检查。

---

## 6. 栈深度

### 6.1 方法与限制

- 构建配置：`build/Debug/compile_commands.json` 的 393 条命令均为 `-O0 -g3 -DDEBUG`。`build/firmware-release/compile_commands.json` 中 `-Os` 与 `-O3` 各出现 391 次，逐文件优化级别未核实。`build/` 下检索 `*.su` 命中 0；Debug 编译数据库检索 `-fstack-usage` 命中 0，因此没有 `.su` 文件。
- ELF：`build/Debug/mp3_project.elf`（2026-10-30 18:07）与 `build/firmware-release/mp3_project.elf`（2026-10-28 12:23），均为 `armv7e-m`。两者都早于 `Service/filesystem/` 最近提交（2026-11-01），静态估算基于更早的构建。
- 工具：`arm-none-eabi-objdump -d --no-show-raw-insn`，解析脚本位于 `scratchpad/research-t188/stack_estimate.py`（不在仓库内）。帧大小取前 40 条指令内 `push`/`vpush` 的字节数，加函数体内 `sub sp, #N` 的最大值。只跟随直接调用（`bl` 与尾跳转）。另手工补入 FatFs `disk_*` 经 `disk.drv[]` 的 5 组间接边（`Middlewares/Third_Party/FatFs/src/diskio.c:42,59,85,107,128`）。
- 未纳入：`blx reg` 间接调用（Ops 表、RawOps、HAL 回调、函数指针），因此结果为下界，且只覆盖直接调用可达的部分。
- 链接裁剪：Debug map 的 `Discarded input sections` 列出 `.text.Service_Filesystem_WriteFile`、`.text.Service_Filesystem_RemoveFile`、`.text.Service_Filesystem_GetFileInfo`（`build/Debug/mp3_project.map:17715-17725`），这些函数不在 ELF 中，估算中缺失。
- 静态估算可能包含互不可能同时发生的路径，例如 `move_window → disk_read`、`dir_next → create_chain`。

### 6.2 结果（单位：字节，own = 本函数帧，deepest = 直接调用链上的最大累计）

| 入口 | Debug（-O0）own / deepest | release own / deepest |
|---|---|---|
| `storage_task` | 40 / 2136 | 20 / 1904 |
| `Service_Filesystem_OpenFile` | 568 / 1376 | 560 / 996 |
| `Service_Filesystem_ReadFile` | 40 / 624 | 24 / 312 |
| `Service_Filesystem_OpenDirectory` | 552 / 1256 | 552 / 892 |
| `Service_Filesystem_ReadDirectory` | 584 / 1248 | 576 / 900 |
| `f_open` | 112 / 808 | 76 / 436 |
| `f_read` | 64 / 584 | 12 / 288 |
| `f_opendir` | 32 / 704 | 24 / 340 |
| `f_readdir` | 24 / 664 | 16 / 324 |
| `f_mkfs` | 120 / 552 | 52 / 280 |
| `disk_read` | 40 / 432 | 4 / 228 |
| `filesystem_flash_transfer_read` | 24 / 336 | 24 / 192 |
| `filesystem_sd_transfer_read_blocks` | 40 / 228 | 28 / 64 |
| `BSP_USER_DISKIO_ReadBlocks` | 32 / 368 | 32 / 224 |

- release 的最深链（1904 B）：`storage_task`(20) → `storage_sd_process`(176) → `storage_catalog_init`(8) → `storage_catalog_music_dfs`(800) → `Service_Filesystem_ReadDirectory`(576) → `f_readdir`(16) → `dir_next`(20) → `create_chain`(12) → `get_fat.isra.0`(24) → `move_window`(24) → `disk_read`(4) → `USER_read`(0) → `BSP_USER_DISKIO_ReadBlocks`(32) → `filesystem_flash_transfer_read`(24) → `Platform_Flash_ReadBlocksStart`(24) → `platform_flash_enable_memory_mapped_mode_internal`(24) → `W25Qxx_QSPI_STM32HALAdapter_EnableMemoryMappedMode`(80) → `HAL_QSPI_MemoryMapped`(32) → `QSPI_Config`(8)。
- Debug 的最深链（2136 B）：`storage_task`(40) → `storage_sd_init`(24) → `storage_catalog_init`(8) → `storage_catalog_music_init`(8) → `storage_catalog_music_dfs`(800) → `Service_Filesystem_OpenDirectory`(552) → `f_opendir`(32) → `follow_path`(32) → `dir_find`(32) → `dir_next`(32) → `create_chain`(40) → `put_fat`(48) → `move_window`(24) → `sync_window`(32) → `disk_write`(40) → `USER_write`(24) → `BSP_USER_DISKIO_WriteBlocks`(32) → `filesystem_flash_transfer_write`(24) → `filesystem_flash_transfer_finish`(56) → `Platform_Flash_ProcessOperation`(16) → `FlashFTL_Process`(96) → `flash_ftl_record_data_is_valid`(32) → `flash_ftl_record_is_committed`(32) → `flash_ftl_commit_is_valid`(24) → `flash_ftl_page_crc_is_valid`(24) → `flash_ftl_crc32`(32)。
- 两条最深链都经过 APP 的 `storage_catalog_music_dfs`（800 B 帧）。该帧是两条链上最大的单帧；链上 Service 帧最大为 `Service_Filesystem_OpenDirectory` 与 `ReadDirectory`，约 552–584 B。

### 6.3 Service 内的大帧来源

- TCHAR 路径缓冲：每个带路径入口约 518 B（`filesystem_file.c:62`；`filesystem_config.h:20` 的 259 TCHAR）；`RenameFile` 有两个（`filesystem_file.c:355-356`），约 1036 B。
- FILINFO：约 548 B，按 `Middlewares/Third_Party/FatFs/src/ff.h:198-204` 的成员推算（`fsize` 4、`fdate`/`ftime` 各 2、`fattrib` 1、`altname[13]` 26 B、`fname[256]` 512 B），未用 `sizeof` 核实。
- `FIL`、`DIR`、`FATFS` 对象在静态槽或全局中，不在栈上（§1.1）。
- 票 #186 记录的板测为“Storage 栈最低剩余 1908 B / 4096 B”（即峰值约 2188 B）。该记录来源于 #186 的 Notes，仓库内未核实，构建变体未知。

---

## 未能核实

1. NOR 实际擦除、编程、状态寄存器写入耗时：仓库无数据手册（`*.pdf` 检索为 0）；§3.4 的数值来自代码注释，未对照原文。
2. 一次 `WriteStart` 的最坏 NOR 操作数，以及块池是否在写路径内联擦除：`Components/flash_ftl/flash_ftl.c` 的写路径未读，§3.4 为推断。
3. `HAL_SD_Init` 与 `HAL_SD_DeInit` 是否停止在途 SDMMC IDMA 与中断：HAL 源码未读，未板测；§4.2 的残余窗口是否可达未核实。
4. `sd_stm32_hal_sync` 以 `HAL_SD_GetCardState()==TRANSFER` 判定 DMA 完成是否充分（`sd_stm32_hal_adapter.c:508-520`）：HAL 行为未核实。
5. §2.7 的 SD 非 owner 清除 `fs_type` 路径：代码路径推断，未主机测、未板测。
6. FatFs 锁表满时的 `FR_TOO_MANY_OPEN_FILES` 返回路径（§1.2 E4，`ff.c:3340`、`:3349`）：代码推断，未测。
7. `Platform_Flash_ClearOperationCallback` 的调用者：未检索。
8. `ff_convert` 所用 CP936 表由 Resource Service 加载：`Service/resource/resource_service.c:22-26` 的注释与 `filesystem_text.c:14` 一致，加载代码未读。
9. `STORAGE_FLASH_AUTO_FORMAT`（`APP/tasks/storage/storage_task_config.h:12`，值 1）与 `APP/tasks/storage/flash/storage_flash.c:8,42` 的“不自动格式化”注释不一致；`storage_flash.c:89-104` 的自动格式化分支在该值下被编译。运行期是否触发未测。自动格式化会销毁 FTL 分区内容（`flash/filesystem_flash_transfer.c:212` 注释）。
10. 栈估算的局限：静态直接调用图；间接调用未纳入；`--gc-sections` 裁剪的符号未纳入；ELF 早于 `Service/filesystem/` 最近提交；Debug（-O0）与 release（-Os/-O3）逐文件优化级别未核实；构建未启用 `-fstack-usage`。
11. 票 #186 记录的板测栈数据（1908 B 最低剩余）：仓库内未核实，构建变体未知。
12. `Service/filesystem/filesystem_config.h:39-41` 注释引用的 SD 规范时限（读 100 ms、写忙 250 ms 与 500 ms）：仓库中无规范原文，未核实。
13. 本票未运行主机测试与 GUI 模拟器，所有结论基于静态阅读。
