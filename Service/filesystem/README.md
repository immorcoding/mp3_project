# 文件系统 Service

本 Module 持有 SD/Flash FatFs 卷生命周期，并同步执行块访问与卷感知文件/目录操作。完成通知不绑定任何任务（每卷一个静态二值信号量，见“请求与事件路径”）。文件接口是线程安全的：每卷一把卷锁在公开函数入口拿，另有一把只罩 `f_open`、`f_opendir` 和按定位符打开的窄表锁；GUI 与播放任务不得调用（入口断言停机）。规则与持锁上界见“并发模型”（ARC-21、[ADR-0024](../../docs/adr/0024-file-interface-concurrency.md)）。SD 一侧的调用者有四个任务：卷任务（启动、刷新、暂停与恢复、挂载与卸载）、音频读取任务（读音频文件，#215）、前台任务（读曲目标签，#218）和后台任务（曲库扫描读目录，#220）；Flash 一侧是 Flash 启动任务（初始化、挂载、格式化）和后台任务（回收，#220；Storage Task 已删除），所以锁第一次真的会被争用；真实并发由第 3 次上板验证。SD 另有一道“闸”和三个卷任务用的入口（启动、刷新、暂停与恢复），见“SD 闸与卷生命周期入口”。仍是一个 Filesystem Module；`sd/` 和 `flash/` 是私有实现分区。公开 Interface 按调用者切开，没有总揽头；卷与路径上限在 `filesystem_types.h`，供文件、目录和生命周期头共用。卡检测、消抖、启动和回收时机由 APP 决定。

当前文件与目录语义见 [ADR-0014](../../docs/adr/0014-filesystem-volume-aware-file-interface.md)。公开接缝切开与 `Reclaim` 命名见 [ADR-0012](../../docs/adr/0012-filesystem-public-seams.md)。当前产品不提供 USB MSC，见 [ADR-0013](../../docs/adr/0013-usb-msc-product-scope.md)。SD 为曲库主介质、Flash FTL 为机内盘与资源安装暂存，见 [ADR-0015](../../docs/adr/0015-volume-roles-and-resource-install.md)。

## 公开 Interface

- `filesystem_types.h`：逻辑卷枚举与 UTF-8 路径/名称上限。文件头、目录头需要 Volume 或长度时只包含本头，互不包含。
- `filesystem_service.h`：卷生命周期。SD 为 `InitSD()/MountSD/UnmountSD`；Flash 为 `InitFlash()`（建立完成信号量并订阅）、`MountFlash`（扫描 FTL 并挂载 FAT）、`UnmountFlash`、`FormatAndMountFlash`、`RecoverAndMountFlash`、`ReclaimFlash`。设备不提供 SD 格式化入口。`Service_Filesystem_DenyCaller(TaskHandle_t)` 登记不得调用文件接口的任务（APP 启动时登记 GUI 与播放任务，见“并发模型”）。`InitSD()`/`InitFlash()` 没有参数：完成通知由 Service 自己的信号量承担，不借用任何任务的通知槽，Service 也不认识 APP 类型。两者各自须在任何任务使用文件接口之前调用一次（SD 由卷任务在 `StartSD` 之后、第一次 `MountSD` 之前调用；Flash 由 Flash 启动任务在启动时调用），调用时拿所属卷的卷锁；`InitSD()` 之后可以重复调用，只重新订阅传输事件，不重建信号量，挂载与卸载路径用它做幂等的就绪检查，`InitFlash()` 重复调用不做任何事。
- `filesystem_card.h`：SD 卡槽的卷生命周期入口，调用者是卷任务（`APP/tasks/volume/sd/volume_sd.c`）。`Service_Filesystem_StartSD(detect_callback, detect_context, &card)`（注册卡检测回调并初始化设备）、`Service_Filesystem_RefreshSD(&card)`（SD 卷锁内刷新，给出在不在、就绪否、是否重新初始化过、容量和诊断，类型 `Service_Filesystem_CardStatusTypeDef`）、`Service_Filesystem_PauseSD()` / `Service_Filesystem_ResumeSD()`（闸）。APP 因此不再需要包含 `platform_sd.h`。
- `filesystem_types.h` 另有失败分类函数 `Service_Filesystem_StatusIsVolumeUnavailable(status)`：`NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 为真，不算文件错误（调用者不写 `READ_ERROR`、不回退、不缓存，先看卷序号）。
- `filesystem_file.h`：卷感知文件访问。`OpenFile/ReadFile/WriteFile/SeekFile/GetFilePosition/GetFileSize/SyncFile/CloseFile/RemoveFile/RenameFile/GetFileInfo`。调用必须给出 Volume；相对路径只在该卷内解释。另有 `OpenFileByLocator`（按文件定位符只读打开，见下节“文件定位符与 FatFs 补丁”）。`ReadFile`/`WriteFile` 单次不超过 `SERVICE_FILESYSTEM_IO_MAX_BYTES`（32 KiB），超过返回 `SERVICE_INVALID_PARAM`，不截短也不在内部拆分。
- `filesystem_directory.h`：卷感知目录访问。`OpenDirectory/ReadDirectory/RewindDirectory/CloseDirectory/CreateDirectory/RemoveDirectory`。空路径表示所选卷的根目录；文件接口不得把空路径当作文件名。`ReadDirectory` 给出的目录项带文件定位符 `Locator`。
- `filesystem_text.h`：文本编码查表。`GbkToUnicode(gbk_code)` 把 GBK 双字节码（首字节在高 8 位）转 Unicode 码点，0 表示无映射；内部调用 FatFs `ff_convert(code, 1)`，只读 CP936 表（Resource Service 在任务启动前就绪），不访问介质、不阻塞，可在任何任务上下文调用。由前台任务读标签时（`catalog_track_text_read()`）作为 GBK 查表回调注入曲目文本函数，FatFs 类型不出本 Module（ARC-12）。
- `filesystem_flash_access.h`：启动诊断的同步包装。`ReadFlashArray/ReadFlashDiagnostic/EraseFlashDiagnostic/ProgramFlashDiagnostic`。这是唯一允许包含 `platform_flash.h` 的公开头，以便使用 Platform 诊断区域枚举。

`FormatAndMountFlash` 显式注销卷、格式化 FTL、建立 FAT12/16 superfloppy，成功返回时 Flash 文件系统必须已经重新挂载。本 Service **不会**在启动或 `MountFlash` 失败时自行调用它。Flash 启动任务（`APP/tasks/volume/flash/`）在 `SERVICE_NO_FILESYSTEM` 时是否调用，由 `VOLUME_FLASH_AUTO_FORMAT` 决定。
`RecoverAndMountFlash` 注销旧卷、恢复硬件并重扫，成功返回时卷已经挂载；旧文件/目录对象不可沿用。
`ReclaimFlash` 给 FTL 一次最多回收一块的机会，水位和目标仍归 FTL，可能什么都不擦。

卷、文件、目录和诊断均返回 `Service_StatusTypeDef`。未格式化或无 FAT 返回 `NO_FILESYSTEM`。卷未初始化或未挂载返回 `NOT_READY`。物理介质当时不可用返回 `NO_MEDIA`：FatFs 给出 `FR_NOT_READY`，或 SD 卷上的调用失败（映射成 `ERROR`）而卡检测脚显示卡已不在（见“`NO_MEDIA`”）。容量不足返回 `NO_SPACE`，失效句柄返回 `INVALID_HANDLE`，损坏/IO 错误返回 `ERROR`，执行器超时返回 `TIMEOUT`，读写字节数超过 32 KiB 返回 `INVALID_PARAM`。FatFs 类型停留在本 Module 和中间件接缝内。

## 路径契约

对外统一使用 UTF-8 相对路径，分隔符为正斜杠，例如 `Music/song.mp3`。禁止盘符、绝对路径、反斜杠、`.`、`..`、路径穿越、非法 UTF-8 以及超过 `SERVICE_FILESYSTEM_PATH_MAX_BYTES` 的路径。文件名和目录名不能为空。Service 内部为 SD/Flash 加上各自盘符，并转换成当前 FatFs TCHAR 编码。上层不能传入 `0:`、`1:`，也不能直接调用 `f_open`/`f_unlink`。

同名路径在两个卷上是彼此独立的对象。文件与目录调用必须给出 Volume；曲库条目只存 SD `Music/` 相对路径，不存卷字段。首版曲库只枚举 SD 的 `Music/`，不扫描 Flash，也不扫描卡根；`Update/` 等安装暂存路径不属于曲库。见 [storage.catalog.md](../../docs/shape/storage.catalog.md)。

## 文件与目录句柄

APP 只持有不透明 Token，`FIL`、`DIR`、TCHAR、FRESULT 和 FatFs 调用均在本 Module 内。静态文件槽与目录槽**按卷固定分配**（`filesystem_config.h`）：SD 4 个文件槽、2 个目录槽，Flash 2 个文件槽、1 个目录槽，共 9 个；一卷的槽用尽后该卷的打开返回 `SERVICE_BUSY`，不影响另一卷。Token 的槽号决定所属卷，所以从 Token 就能算出该拿哪把卷锁。句柄校验 Token、槽位、代次和所属 Volume。一卷卸载、SD 拔出、Flash 格式化或恢复后，该卷全部 Token 立即失效，返回 `SERVICE_INVALID_HANDLE`；另一卷的 Token 不受影响。

打开方式为只读、仅新建、读写已有文件或追加。仅新建遇同名文件返回 `SERVICE_BUSY`，不截断或覆盖。读写必须检查实际字节数：短读可能是 EOF，短写可能是容量不足，`SERVICE_OK` 不能代替长度检查。`WriteFile` 成功只表示本次数据已交给文件系统写入流程；`SyncFile` 对应 `f_sync`，返回成功前完成当前文件的 FatFs、FTL 和 Flash 写入语义。`CloseFile` 按值传递，成功后 Token 内部失效；关闭路径会执行必要同步，不得静默丢弃数据。

所有函数同步执行，在调用者任务里完成，普通 APP 缓冲不直接交给 DMA。没有跨任务文件请求队列；GUI 与播放任务不得调用本同步接口（`Service_Filesystem_DenyCaller`，入口断言停机）。

`RemoveFile` 只删除文件，不删除目录；文件不存在按成功处理，目标仍打开时拒绝删除。无 TRIM 时，删除释放 FAT 簇并同步元数据，不把文件数据的 LBA 直接通知 FTL 作废。后续 LBA 重写完成后，旧物理版本才成为可 GC 的失效块。文件删除不是安全擦除。非空目录删除返回 `SERVICE_BUSY`。

## 文件定位符与 FatFs 补丁

FatFs 按路径打开每次从文件夹第 0 个目录项起逐项比较长文件名，耗时随文件在文件夹内的位置线性增长。为让 12000 首同在一个文件夹时打开仍然够快，`ReadDirectory` 随每个条目给出不透明的**文件定位符**（`Service_Filesystem_FileLocatorTypeDef`，16 B，类型在 `filesystem_types.h`），`OpenFileByLocator` 按它直接读出目录项并打开。依据 [ADR-0022](../../docs/adr/0022-fatfs-open-by-locator-patch.md)、ARC-19。

- **内容与有效期**：文件夹起始簇、主（短名）目录项在文件夹内的字节偏移（定位），文件起始簇、文件长度（核对）。布局在私有头 `filesystem_locator.h`，调用者只能整体保存、拷贝、传回。全零表示“无定位符”（目录读到末尾时恰好落在扇区末尾且无法确定位置的个别条目、空文件）。只在同一曲库代次内有效，不跨卷、不跨换卡。
- **取得**：`filesystem_directory.c` 在 `f_readdir` 之后从 `DIR` 推算主目录项位置，并直接读 FatFs 窗口里（必要时多读一次该扇区）的目录项字节，与 `FILINFO` 的属性和长度互相核对后才发出定位符；FatFs 窗口在跨簇时会被换成 FAT 扇区，此时多一次 `disk_read`。
- **打开**：`OpenFileByLocator(volume, locator, file, status)` 返回三态而不是复用 `Service_StatusTypeDef`，让“核对不符，退回路径打开”与真正的失败可区分：`OPENED`；`MISMATCH`（无定位符、目录项已删除、是目录、起始簇或长度不同、位置越界）未占用文件槽，调用者退回 `OpenFile`；`FAILED`（卷未就绪、槽用尽、锁表满、被写方式持有等）原因见 `status`。只读。
- **补丁**：核对与填 FIL 由 `ff.c` 的一个补丁函数 `f_open_dirent` 完成（`patches/fatfs/0001-open-by-dirent.patch`），像 `f_open` 结尾那样登记锁，`CloseFile` 照常释放。原型只在私有头 `filesystem_ff_patch.h`，不进 `ff.h`、不改 `ffconf.h`。补丁被 CubeMX 重新导出覆盖时固件链接失败（不会静默退回慢路径）；重打见 `patches/fatfs/README.md` 与 `scripts/apply-vendor-patches.ps1`。
- **耗时**：与文件夹内的项数无关，只随目标所在的目录簇序号增长（沿簇链走到目标簇，每个目录簇至多一次 FAT 扇区读，32 KiB 簇的 12000 首同文件夹约 54 个簇），而按路径要读完该项之前的全部目录扇区（约 3500 个）。主机测试见 `Tests/filesystem_locator`。

## 并发模型

规则来自 [ADR-0024](../../docs/adr/0024-file-interface-concurrency.md) 与 ARC-21，实现在私有的 `filesystem_lock.c/.h`。

- **每卷一把卷锁**（SD 一把、Flash 一把，FreeRTOS 静态互斥量，带优先级继承，不占堆），只在公开函数入口拿、返回前放：一次调用只拿一次，多步函数中途不放。卷锁覆盖所属卷的 FatFs 卷对象、文件与目录槽、卷状态（初始化与挂载标志）、完成信号量、SD 的 DMA 中转缓冲和 Flash 的 Platform 在飞操作，所以设备状态由卷锁保护，APP 不绕过。两卷互不影响：SD 读 32 KiB 与 Flash 写 32 KiB 可以同时进行。
- **不开递归互斥量**：公开函数之间不互相调用，共用部分是不加锁的 `_locked` 内部函数（`FormatAndMountFlash`、`RecoverAndMountFlash` 的卸载、格式化、挂载各步都在同一次持锁里依次调用）。`_FS_REENTRANT` 保持 0，`filesystem_handle.c` 有静态断言。
- **句柄有效性在锁内检查**：Token 的槽号越界（含全零 Token）在拿锁之前就能判为 `INVALID_HANDLE`，不读任何共享状态；槽号合法的一律拿卷锁之后再核对占用、代次与卷状态。
- **窄表锁**：FatFs 的锁表 `Files[]` 两卷共用。只有 `f_open`、`f_opendir` 和按定位符打开的 `f_open_dirent` 三处在卷锁内再拿表锁，顺序固定为卷锁 → 表锁，表锁内不拿别的锁。依据是读 `ff.c`：跨卷会冲突的只有 `inc_lock` 里“找空位并占用”这一步；`dec_lock`（`f_close`、`f_closedir`）和 `clear_lock`（`f_mount`）只写本卷的条目，`chk_lock`、`enq_lock` 只读。这不是并发证明，它依赖一个编译器事实：`dec_lock` 必须先写 `ctr`、后写 `fs`（最后一步才把条目标成空闲），否则别的卷可能在 `fs` 已清零、`ctr` 尚未写入的间隙里占走这一项，随后被迟到的 `ctr` 写入冲掉。Release 反汇编的核对结果记在 #208 的交付说明里；**升级 FatFs 时复核** `inc_lock`、`dec_lock`、`clear_lock`、`chk_lock`、`enq_lock`，并重做反汇编核对。没有给 `ff.c` 加补丁。
- **槽按卷固定分配**，不要第三把锁：槽数见上节，`_FS_LOCK`（9）静态断言不小于槽总数，所以锁表不会先满；槽用尽返回 `SERVICE_BUSY`。
- **单次读写上限 32 KiB**（`SERVICE_FILESYSTEM_IO_MAX_BYTES`，与 SD 的 DMA 中转缓冲同大）：超过返回 `SERVICE_INVALID_PARAM`，不悄悄截短也不在内部拆分，这样一次读写的持锁时长有界。现有调用者都在上限以内：音频块 32 KiB（`AUDIO_READER_BLOCK_BYTES`）、曲目标签块 4 KiB、音频探测头尾 10 B 与 160 B、SD 基准 32 KiB、Flash 文件基准 4 KiB 与一次 1 B 读回；这些常量在各自的源文件里有 `_Static_assert` 不超过上限。
- **调用者限制**：APP 启动时用 `Service_Filesystem_DenyCaller` 登记 GUI 与播放任务（至多 `FILESYSTEM_DENIED_CALLER_MAX` 个）。每个会等锁的公开函数（文件、目录、卷生命周期、Flash 诊断）第一件事是比较当前任务句柄，登记过的任务进来就 `configASSERT` 停机，Release 也保留，检查在等锁之前。`Service_Filesystem_GbkToUnicode` 只读 CP936 表、不拿锁，不受限制。用禁用名单是为了新增任务时共用层不用改。
- **进门序列**：文件与目录入口（共 18 个）的拿锁部分统一走私有的 `filesystem_entry_enter` / `filesystem_entry_leave`：拒绝名单 → 卷没挂就返回 → SD 等闸 → 拿卷锁 → 拿到后再确认卷还挂着 → 做事 → 出门时锁内给失败分类 → 放锁。卷生命周期入口（Init、Mount、Unmount、启动、刷新、Flash 的格式化、恢复、回收和诊断）不走这条序列。细节见下一节。
- **死锁规则**：绝不同时持两把卷锁，跨卷操作由调用者分成多次调用；顺序固定卷锁 → 表锁；持文件锁时不调用会阻塞的接口（`Service_Log_Post` 不阻塞，可以调用）。同一任务重复拿同一把锁、持一把卷锁时再拿另一卷的锁、持表锁时再拿卷锁，都是 `configASSERT` 停机。
- **锁的创建**：三把锁在第一次用到时创建（临界区内幂等，静态存储），早于任何一次持锁，不要求哪个初始化函数先于哪个调用者。

### 其他共享状态的归属

| 状态 | 归属 |
|---|---|
| 文件槽、目录槽、卷的初始化与挂载标志、`SDFatFS`/`USERFatFS`、FatFs 的 `FatFs[]` 卷表项 | 所属卷的卷锁（槽按卷分区） |
| SD 完成信号量、事件值、DMA 中转缓冲、中止/钩子日志行的静态缓冲、钩子的读计数与探测计数 | SD 卷锁 |
| Flash 完成信号量、Platform 在飞操作、格式化工作区 | Flash 卷锁 |
| 目录定位符采集用的扇区缓冲 | 所属卷的卷锁（每卷一份，两卷的 `ReadDirectory` 可以同时进行） |
| FatFs 锁表 `Files[]` | 见窄表锁：打开三处用表锁；关闭与卸载只写本卷的项 |
| FatFs `Fsid`（挂载计数，`++Fsid`） | 两卷的挂载可能同时递增而丢一次更新；它只用于同卷旧对象的校验，Service 在卸载时就丢弃了该卷全部对象，所以无影响 |

### 持锁上界

拆不开的长操作靠调用者约定，由持锁统计核对（实测来自 #202 B 节，Debug，1 张卡）：

| 操作 | 持哪把锁 | 实测（Debug，1 张卡） | 约束 |
|---|---|---|---|
| SD 读 32 KiB | SD | 平均 3.1–3.9 ms，最长 6.85 ms | 上限写进接口 |
| Flash 写 32 KiB | Flash | 约 1.5 s（由 64 KiB 最长 3.04 s 推算，未测） | 后台任务 |
| 按定位符打开 | SD + 表 | 0.35–0.76 ms | 曲库文件一律走它 |
| 按路径打开，12000 首同文件夹的最后一首 | SD + 表 | 1.43–2.05 s，期间音频读取等 SD 锁最长 1.16 s | 只用于小目录和定位符失效时的回退 |
| 新建文件的 `f_open`（Flash） | Flash + 表 | 最长 10.1 ms | 无 |
| 删除文件（Flash） | Flash | 约 138 ms/MiB | 只放后台任务；SD 上的大文件未测 |
| 改名、同步（Flash） | Flash | 66–81 ms、122–134 ms | 无 |
| 回收 | Flash | 一块约 40–50 ms | 一次最多一块 |
| 插卡初始化 | SD | 约 186 ms | 此时没有在读的音频 |
| 刷新入口（`RefreshSD`，含重新初始化） | SD | 无变化时微秒级；重新初始化约 186 ms（同“插卡初始化”）；刷新时若有在途读写则先等它放锁 | 只由卷任务在消抖结束后调用 |
| `MountFlash` | Flash | 3.3–8.3 s（8.3 s 的原因没查） | 只在启动时 |
| 故障路径：SD 单块超时 | SD | 500 ms 等待 + 显式中止上界 100 ms（含 CMD12、DPSM 静止、卡回到 TRANSFER；停不住再加一次 `HAL_SD_DeInit`，微秒级），约 0.6 s；一次请求在第一个失败的分块就返回，所以一次调用不会超过这个数 | 不算正常上界，也不进 R2.1（R2.1 量的是正常读）；小于播放侧 T_data（现在 2 s） |
| 故障路径：Flash 写 | Flash | 写预算 120 s | 不算正常上界 |
| 迟到 DMA 钩子的一次探测（仅钩子构建） | SD | 目标块读一次、受害读等 1 ms 再中止、目标块再读一次，约 10–15 ms（估计，未测），加在被探测的那次读上 | 钩子构建里持锁统计的 SD 最长持有会因此超过 R2.1 的 20 ms，**R2.1 必须用钩子关闭的构建量** |

### 持锁统计

编译开关 `FILESYSTEM_LOCK_STATS_ENABLE`（`filesystem_config.h`）：固件 Debug 开（没有 `NDEBUG`），Release 关（带 `-DNDEBUG`，整段编译掉，不占时间也不占字段）。每把锁（SD、Flash、表）只记开机以来的最长持有、最长等待、各自对应的操作和拿取次数，用周期计数器量（微秒；区间超过 4 s 或计数器未启动时改用 tick，毫秒精度），不记逐次日志。

只出汇总日志，由放掉卷锁的那次公开调用顺带投：距上次汇总满 `FILESYSTEM_LOCK_STATS_PERIOD_MS`（10 s）就投一批，每把锁一行，共 3 行，**日志在锁放掉之后投，不在持锁时投递**；没有文件接口调用就没有汇总。格式（标签 `FSLOCK`，数值是开机以来的最大值，不清零）：

```
FSLOCK sd n=9 hold=4000us:Open wait=0us:-
FSLOCK flash n=7 hold=7000us:Reclaim wait=2500us:Info
FSLOCK table n=1 hold=4000us:Open wait=0us:-
```

`n` 是拿取次数；`hold`/`wait` 后面是最长一次的微秒数和对应的操作（持有的操作；等待的操作是等锁的那个调用），`-` 表示没有。操作名：`Open`、`OpenLoc`（按定位符）、`OpenDir`、`Read`、`Write`、`Seek`、`Tell`、`Size`、`Sync`、`Close`、`Remove`、`Rename`、`Info`、`ReadDir`、`Rewind`、`CloseDir`、`MkDir`、`RmDir`、`Init`、`Mount`、`Unmount`、`Format`、`Recover`、`Reclaim`、`FlashDiag`、`Start`、`Refresh`。每行最长约 80 个字符，远小于 Service Log 单条正文的 127 个字符。消息池核算见 [Service Log](../log/README.md)（DIAG-3）。

## SD 闸与卷生命周期入口

来自 [卷生命周期与卷事件](https://github.com/immorcoding/mp3_project/issues/195) 的 Service 部分，由 #210 落地，#211 的卷任务接线（`APP/tasks/volume/sd/volume_sd.c` 是唯一调用者，APP 不再包含 `platform_sd.h`）。卷任务在边沿处关闸、静默 30 ms、对账、开闸；调用者对 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 不当文件错误。

### 入口签名

```c
typedef void (*Service_Filesystem_CardDetectCallback_t)(void *context);   /* ISR 里调用 */

Service_StatusTypeDef Service_Filesystem_StartSD(Service_Filesystem_CardDetectCallback_t detect_callback,
                                                 void *detect_context,
                                                 Service_Filesystem_CardStatusTypeDef *card);
Service_StatusTypeDef Service_Filesystem_RefreshSD(Service_Filesystem_CardStatusTypeDef *card);
void Service_Filesystem_PauseSD(void);
void Service_Filesystem_ResumeSD(void);
```

`Service_Filesystem_CardStatusTypeDef`：`Present`（卡检测脚表示有卡）、`Ready`（设备 READY）、`Reinitialized`（本次调用把卡（重新）初始化过）、`CapacityBytes`、`BlockSize`（`Ready` 时有效）、`DeviceError`、`PortStatus`（最近一次设备错误的诊断）。失败时也尽力填写：卷任务在“刷新失败但卡不在”时同样要卸载。

- `StartSD`：只调用一次（Platform 不允许重复初始化，第二次返回 `ERROR`）；无卡同样成功，此时 `Ready` 为假。它只做 Platform 一侧，卡就绪后卷任务还要调 `InitSD()` 再 `MountSD()`，次序与原来的 Storage Task 一致（现在由卷任务调用）。
- `RefreshSD`：包 `Platform_SD_Process`。`Reinitialized` 取自 Platform 的 INSERTED 事件，它按进入时的设备状态判断，所以“读写失败时设备被先写成无卡、卡又回来”（被吞的拔出，#191 的 `via=presence`）也报告为 `Reinitialized`。卡在而设备 ERROR 又没有边沿时，刷新成功但 `Ready` 为假，不自动恢复。
- `PauseSD` / `ResumeSD`：关闸、开闸。二者是标志，不是计数：消抖期间每个新边沿再关一次没有副作用，一次开闸就够。不拿卷锁、不阻塞，不检查拒绝名单（不等任何东西）；`StartSD` / `RefreshSD` 要等 SD 卷锁，所以和其他会等锁的入口一样，GUI 与播放任务调用会断言停机。

### 为什么刷新和启动必须持 SD 卷锁

CubeMX 生成的 `FATFS/Target/sd_diskio.c` 在 `SD_read` / `SD_write` 成功之后有一个**没有超时**的 `while (BSP_SD_GetCardState() != MSD_OK) {}`，而 `BSP_SD_GetCardState` 在 Platform 状态不是 READY 时返回 `BUSY`。设备状态若在别的任务持锁读写的**中途**变成非 READY（刷新发现拔卡、中止停不住进 ERROR），读写的调用者会持着 SD 卷锁在那个循环里空转。所以改 Device 状态的入口（`Platform_SD_Refresh`、`Platform_SD_Process`、`Platform_SD_Init`）必须与读写互斥，这正是 `RefreshSD` 要“SD 卷锁内刷新”的原因，也是 ARC-21 “设备状态由卷锁保护，APP 不绕过”在 APP 一侧的落实方式。生成文件不能改，不要靠改它解决。

### 闸

- **实现与创建时机**：一个静态事件组（`StaticEventGroup_t`）的 1 位，置位表示开。第一次用到时（`PauseSD`、`ResumeSD` 或第一个 SD 的文件与目录入口）在临界区内创建，初始为开，所以不要求哪个初始化函数先于哪个调用者。选事件组而不是二值信号量：开闸要一次叫醒所有在等的调用者，事件组置位一次放行全部，二值信号量只能一个传一个；它也不占信号量，三把互斥锁的个数不变。固件里 `event_groups.c` 本来就在内核构建里，`configUSE_EVENT_GROUPS` 取默认值 1，没有改 `FreeRTOSConfig.h`。
- **顺序**：文件与目录入口先查拒绝名单，再做进门序列（闸在拿锁之前、拒绝名单检查之后）：卷没挂就立刻返回 → SD 等闸 → 拿卷锁 → 拿到后再确认卷还挂着。
- **开闸时多个等待者怎样醒**：`xEventGroupSetBits` 一次把所有在等的任务放进就绪表，之后由调度器按优先级决定谁先跑，它们各自去拿 SD 卷锁，由互斥量的优先级继承保证高优先级先拿到。没有先后顺序的保证，也不需要：闸只表示“不要在卷任务对账时挤进来”，不表示排队。
- **只挡 SD，不挡 Flash**：Flash 入口不看闸（Flash 没有拔插）。
- **不挡卷任务自己**：`InitSD`、`MountSD`、`UnmountSD`、`StartSD`、`RefreshSD` 都不走进门序列，关闸期间照常执行。
- **已持锁的调用不打断**：闸只在拿锁之前检查。已经通过闸、正在等卷锁的调用也不被追回：它们拿到锁之后由第二次“卷没挂”检查兜住（下面），卷任务则靠拿 SD 卷锁等在途调用退出。
- **持锁等闸是死锁，会断言停机**：卷任务要这把锁才能对账和开闸。等闸之前先断言调用者不持任何 Filesystem 锁（`configASSERT`，Release 也保留；闸开着时同样检查，错误因此每次都暴露而不是只在关闸时）。同样，持锁调用本身的拿锁断言（#208）照旧：持 SD 卷锁再进 SD 入口、持一卷再进另一卷都会停机；这些新路径没有持锁等闸，也没有持锁拿另一把卷锁。

### 等闸有没有上界，谁保证开闸

**没有上界**，这是设计（#195：连续抖动不另设上限，由播放侧的 T_data 兜底）。`INCLUDE_vTaskSuspend` 为 0，`portMAX_DELAY` 在内核里只是约 49.7 天的有限超时，等闸的循环在超时后继续等，所以调用者不会在闸没开的情况下往下走。**保证开闸的是卷任务**：每次 `PauseSD` 之后，在所有路径上（对账成功、挂载失败、刷新失败、卡一直 ERROR）都必须配一次 `ResumeSD`，否则所有 SD 调用者会一直等下去。Service 不设看门狗，也不在超时后偷偷放行，因为放行会让调用者在对账中途挤进去。

等闸期间调用者不持任何锁，所以它再久也不挡别的调用者，更不挡卷任务拿锁；等待是睡眠，不占 CPU，等待任务的优先级再高也只是睡着，不会因优先级与卷任务倒置（倒置只会发生在持锁等待时，而持锁等闸被断言禁止）。时长由卷任务的收尾顺序决定：静默 30 ms 加对账（正常几十毫秒，故障路径加上约 0.6 s 的在途调用）。

### “卷没挂”的两次检查分别防什么

卷的挂载标志只由持卷锁的任务写，读它是一个字节，不需要锁（声明为 `volatile`），所以拿锁前的读只是提示，拿锁后的读才是依据。

1. **拿锁前一次**（SD 另在等闸之后再看一次）：卷没挂就立刻返回，不等闸也不等锁。防的是**白等**：`MountFlash` 持 Flash 卷锁 3.3 至 8.3 s，插卡挂载持 SD 卷锁约 186 ms，调用者没有必要陪着等完才得到 `NOT_READY`。等闸之后再看，是因为等闸的这段时间里卷可能已被卸载，这样开闸后不去拿锁。
2. **拿到锁后一次**：防的是**检查与拿锁之间的竞争**：第一次检查通过后、拿到锁之前，卷任务在锁内卸载了卷（或调用者在别的任务持锁期间被卸载）。拿到锁后发现卷没挂，放锁并返回，不碰 FatFs。

返回值：按路径或卷操作的入口（打开、按定位符打开、删除、改名、建删目录、取信息）给 `NOT_READY`；按句柄操作的入口（读写、定位、同步、关闭、读目录、回到开头、关目录）给 `INVALID_HANDLE`，因为卷没挂时其上所有句柄都已失效（与原行为一致）。两者对调用者都是“不当文件错误，先看卷序号”。参数、路径、槽号越界的句柄仍在拿锁之前被拒，那不是共享状态的问题。

### `NO_MEDIA`

读写失败时，在**锁内**看卡检测脚：SD 卷上的调用以 `SERVICE_ERROR`（FatFs 的磁盘错误等没有更具体语义的失败）结束，而脚显示卡不在，就把 `ERROR` 改成 `NO_MEDIA`，放锁后返回。

- **只在失败时看**，且只看 `ERROR`：成功、`BUSY`、`INVALID_PARAM`、`NO_SPACE`、`NO_FILESYSTEM` 等都原样返回，不读脚；Flash 没有脚，不分类。目录与路径类入口也用同一个出口，所以同样会得到 `NO_MEDIA`（主机上 FatFs 的扇区窗口缓存着目录扇区，很难让它们读盘失败，所以主机测试用文件数据读，出口是同一段代码）。
- **在锁内**：这样“失败”和“卡不在”是同一刻的结论，不会夹进卷任务的卸载。
- **卡检测脚的读取路径在任务上下文安全**：`filesystem_sd_transfer_card_present()` → `Platform_SD_IsPresent()` → `SDCard_IsPresent` → Adapter 的 `HAL_GPIO_ReadPin`，只是一次 GPIO 输入寄存器读取，不碰 SDMMC、不改任何状态、不阻塞、不拿别的锁。
- **已知漏洞**（推断，板上没测到）：数据脚比检测脚先断时，第一次失败仍是 `ERROR`，调用者按今天的做法报读取错误；之后卡检测脚到位，后续调用得到 `NO_MEDIA`。
- 失败后 FatFs 把对象标成出错：同一个 `FIL` 的后续调用会得到 `FR_INT_ERR`（同样映射成 `ERROR`，所以同样改成 `NO_MEDIA`），`CloseFile` 关不掉；这些对象和槽由卷任务的 `UnmountSD` 一起释放（卸载释放本卷全部槽，主机测试覆盖）。

### 卷任务怎么用这些入口（#211）

- 卷任务（`APP/tasks/volume/sd/volume_sd.c`）已经切到 `StartSD` / `RefreshSD`，不再直接调 `Platform_SD_*`，APP 不再包含 `platform_sd.h`。上面那个无超时的 `while (BSP_SD_GetCardState() != MSD_OK)` 循环因此不会让读写者持锁空转：改 Device 状态的入口都在 SD 卷锁内，状态只能在不持锁的间隙变化。
- 每次 `PauseSD` 都配 `ResumeSD`，所有路径（对账成功、挂载失败、刷新失败、换卡、无卡）都配；卷任务的做法是“闸只在一次消抖过程的第一个边沿关一次，每次过程以一次开闸结束；对账期间又来了新边沿则闸保持关闭、重新消抖”，主机测试穷举 384 种组合核对。卷任务自己的挂载、卸载、刷新不受闸挡，所以关着闸调用它们是正常用法。
- 收尾顺序里的“拿 SD 卷锁等在途调用退出”是 `RefreshSD`（和 `UnmountSD`）入口自己的拿锁完成的，不需要另一个入口。已知窗口：`RefreshSD` 和 `UnmountSD` 是两个独立的持锁入口，放锁到再拿锁之间已通过闸、正在等锁的调用可能抢到锁，见 [卷任务](../../APP/tasks/volume/README.md)“已知窗口”。
- 收到 `Service_Filesystem_StatusIsVolumeUnavailable(status)` 为真的结果，回到等待点先看卷序号（做法见 [音频读取任务](../../APP/tasks/audio_reader/README.md)“失败约定”和 [曲库](../../APP/apps/music/catalog/README.md)“扫描单步与错误分类”）。
- `StartSD` 之后卷任务仍调 `InitSD()` 再 `MountSD()`；`RefreshSD` 返回 `ERROR` 时 `card` 仍填写，“刷新失败但卡不在”以及“刷新失败而卡在、设备进了 ERROR”都卸载旧卷，刷新失败时不挂载。


## 主机测试的保真边界

`Tests/flash_ftl`（`filesystem_lock_tests`、`filesystem_lock_release_tests`）与 `Tests/filesystem_locator` 经公开接口验证加锁，用共用 FreeRTOS 桩的互斥锁：可以脚本化“拿不到锁”“别的任务占着锁、等锁时放掉”，并读每把锁的拿放次数。通过证明：每次公开调用对所属卷的锁拿放各一次（成功和锁内的失败路径都一样）、另一卷的锁不碰；只有三个打开入口等表锁，且等表锁时只持自己的卷锁；槽按卷用尽返回 `SERVICE_BUSY`；32 KiB 上限；拒绝名单在等锁之前停机；重入同一卷、持一卷再进另一卷、持表锁再进卷入口都断言停机；统计的数字与格式，日志在锁放掉之后投，Release 构建不投。

**主机测试是单线程的，桩不切换任务也不真的阻塞，通过不等于并发没问题。** 不证明：真实的任务抢占与优先级继承、两卷同时进行的 DMA/QSPI、窄表锁在真实交错下的正确性（取决于上面的 `dec_lock` 写入顺序与 FatFs 内部行为）、持锁和等锁的真实时长、中断时序、栈余量。这些留给板级验收（#202 的 R2.x、R6.x）。

**闸、`NO_MEDIA`、卷没挂检查、启动与刷新的主机测试**在 `Tests/flash_ftl` 的 `filesystem_card_behavior`（`test_filesystem_card.c`）和 `Tests/filesystem_locator` 里，用共用桩的事件组与阻塞钩子脚本化“闸关着时有人在等”：调用者将要在闸上阻塞时，钩子替卷任务开闸或先卸载卷。**这些用例只证明分支走对了**：等闸发生在拿锁之前（等待时 SD 卷锁一次都没拿）、开闸之后正常完成并平衡拿放；Flash 与卷生命周期入口不等闸；已持锁的调用被关闸打断不了；卷没挂时不等锁不等闸；拿到锁之后发现卷被卸载会放锁并返回；读失败且卡检测脚显示卡不在返回 `NO_MEDIA`，脚在锁内读；启动与刷新在 SD 卷锁内调用 Platform。**不证明**：真实的多任务交错（开闸时多个等待者按优先级怎样醒、醒来后谁先拿到锁）、事件组在真实内核里的阻塞与放行、优先级继承、闸关着期间真实的等待时长、拔卡瞬间脚与数据线断开的先后（见“已知漏洞”）、`Platform_SD_*` 在真实硬件上的行为（Platform 在这些测试里是脚本化的替身）。这些靠板级验收（#202 的清单 3）。

## 私有实现与编译依赖

`filesystem_path.c` 校验 UTF-8 相对路径并加盘符转 TCHAR。`filesystem_handle.c` 管理静态 `FIL`/`DIR` 槽（按卷分区）与代次 Token。`filesystem_lock.c/.h` 是卷锁、表锁、拒绝名单与持锁统计。`filesystem_entry.c/.h` 是文件与目录入口共用的进门和出门序列，并持有 SD 的闸（含公开的 `PauseSD` / `ResumeSD`）。`sd/filesystem_sd_card.c` 是 `StartSD` / `RefreshSD`。`filesystem_file.c` / `filesystem_directory.c` 实现公开文件与目录 API。`filesystem_status.c` 合并 FRESULT 映射与盘符路径。`filesystem_file_locator.c` 实现按定位符打开，`filesystem_locator.h` 是定位符布局，`filesystem_ff_patch.h` 声明 `ff.c` 补丁函数。`sd/filesystem_sd_bsp.c` 强定义 BSP_SD_*，`sd/filesystem_sd_transfer.c/.h` 执行 SDMMC DMA。`flash/filesystem_flash_bsp.c` 强定义 BSP_USER_DISKIO_*，`flash/filesystem_flash_transfer.c/.h` 执行 Flash 请求并等待通知；诊断实现在 `flash/filesystem_flash_access.c`。

Service 包含 FatFs 卷对象/公开 API、中间件 BSP 契约、Platform SD/Flash 产品接口、原生 FreeRTOS、Log Service，以及 Cortex-M7 周期计数器 Adapter（`Adapters/cortex/cycle_counter`：持锁统计在 Debug 里用它量持有与等待，SD 显式中止在所有构建里用它量耗时写 `FSABORT` 日志；计数器没启动时改用 tick，毫秒精度）。FATFS 生成 Glue 只包含其契约，不包含 Service 头。FTL/W25Qxx/HAL 不作为 Service 的直接实现依赖。

## 请求与事件路径

FatFs disk_* → SD 或 USER BSP 强定义 → 私有执行器 → Platform。执行器等待**每卷一个静态二值信号量**（SD 一个、Flash 一个，[ADR-0024](../../docs/adr/0024-file-interface-concurrency.md)）：正在执行传输的任务（调用者，不绑定固定任务）等待，SDMMC/QSPI 中断经 Adapter 和 Platform 已注册的回调给出。卡检测由 APP 自己处理，不进入 Service。Flash 的唯一订阅和等待由本 Module 持有，诊断和逻辑请求共用同一个信号量。APP 启动诊断直接调用 `filesystem_flash_access.h`，不再经过 APP 转发层。

通知只是唤醒提示，最终结果由普通上下文 Process 确认。ISR 不访问缓冲、不维护 Cache、不记录日志或提交下一笔操作。Flash 软件阶段主动让出调度，硬件阶段有界等待信号量；超时后仍须完成安全收尾，控制器无法停止时不得提前返回并复用缓冲。

### 完成来源

- **SD**：SDMMC1 中断 → STM32 SDMMC IRQ Adapter → Platform SD 的传输回调（`InitSD` 经 `Platform_SD_SetTransferCallback` 订阅）→ SD 执行器的回调：先把事件值（读完成、写完成、错误、中止）写进一个 `volatile` 变量，再 `xSemaphoreGiveFromISR`。任务拿到信号量后才读事件值并交给 `Platform_SD_CompleteTransfer()`。同一次等待内后到的事件覆盖先到的（与原先覆盖式通知一致）。等待超时则不经事件值，改为显式中止（见“显式中止”）。
- **Flash**：QSPI/MDMA 中断 → STM32 QSPI IRQ Adapter → Platform Flash 的操作回调（`InitFlash` 经 `Platform_Flash_SetOperationCallback` 订阅）→ Flash 执行器的回调：只 `xSemaphoreGiveFromISR`，不带事件值。信号只是提示，结果以 `Platform_Flash_ProcessOperation()` 为准，且每 `FILESYSTEM_FLASH_WAIT_SLICE_MS` 重查一次，所以信号被合并或丢失也只是晚一个周期。
- 两个回调都只用 `FromISR` 版本，并把 `higher_priority_task_woken` 交给 `portYIELD_FROM_ISR`，不做别的事。回调对应的中断优先级必须满足 `configMAX_SYSCALL_INTERRUPT_PRIORITY`（与改用信号量之前调用 `xTaskNotifyIndexedFromISR` 的要求相同）。

### 信号量何时创建

静态存储（`StaticSemaphore_t`），不占堆。句柄在第一次 `InitSD()` / `InitFlash()` 里创建，**先创建、后向 Platform 订阅回调**：Platform 只在订阅之后才会调用 ISR 回调（SD 的传输中断节点要在回调存在且卡就绪时才安装；Flash 的 `Start*` 在回调未设置时直接拒绝，没有在飞操作就没有中断事件），而且没有 `Start*` 就没有完成中断，所以任何一次完成中断到来时信号量都已经存在。初始化之前调用传输函数得到失败（SD 返回 `false`，Flash 返回 `SERVICE_NOT_READY`），不会去等一个不存在的信号量。`InitSD()`/`InitFlash()` 在所属卷的卷锁内调用，所以第一次调用不与别的调用并发。

### 迟到的旧信号

超时之后才到达的中断会让信号量保持置位，没有任务在等。处理方式：

- **清理时机**：每次**启动传输之前**（SD：每个 DMA 分块的 `Platform_SD_Start*Blocks` 之前；Flash：`filesystem_flash_transfer_finish` 进入时，此时 Start 已经发生但还没有第一次 `Process`）用 `xSemaphoreTake(…, 0)` 清掉。SD 不能在启动之后清，否则会丢掉本次传输自己的中断；Flash 可以在 Start 之后清，因为第一次 `Process` 才是权威，清理之前到达的完成已经体现在 Platform 状态里。
- **SD 在清理之后、本次启动之前才完成的窗口，由显式中止关掉**（#209）：中断说不出是哪次传输结束的，信号量清理清不到这个窗口，所以不靠清理而靠顺序——等待超时后先 `Platform_SD_AbortTransfer()` 让硬件静止（`STOPPED`：数据通路空闲、SDMMC 中断全部屏蔽、IDMA 已关；`NO_MEDIA`、`STUCK`：SDMMC 已断电关时钟、NVIC 挂起位已清），**之后**才 `xSemaphoreTake(…, 0)` 清掉信号量和事件值，Device 要等中止返回才离开 BUSY，同一个任务持着 SD 卷锁，下一笔 Start 只能在这之后发生。中止返回之后不会再有被中止那一笔的中断：此前已经发生的中断在任务被打断之前整个跑完了（单核，任务与 ISR 不会同时运行），它给出的信号被这次清掉；此后外设已经不再请求中断。启动前的 `prepare_wait` 清理作为第二道保险保留。`STUCK` 时 Device 保持 ERROR，`Start*` 被拒，要等卡重新初始化（拔插卡或显式再 Init）才有下一笔传输，重新初始化会完整复位 HAL 句柄。主机测试 `test_sd_completion_that_lands_during_abort_is_cleared_after_it` 覆盖“中断落进中止期间”；真实的 ISR 时序靠第 2 次上板。
- **Flash 不会返回后还有 DMA 在飞**：超时只触发 `Abort`，执行器继续 `Process` 直到控制器静止才返回，所以返回之后到的信号最多是最后一次完成中断没被取走；它被下一次进入时清掉，即使没清，最坏也只是第一次等待提前醒来一次，`Process` 仍报忙就继续等。

### 显式中止（#209）

**预算来自 BSP，不来自 FatFs。** CubeMX 生成的 `sd_diskio.c` 传给 `BSP_SD_ReadBlocks/WriteBlocks` 的超时是 `SD_TIMEOUT`，即 HAL 的 `SDMMC_DATATIMEOUT`（0xFFFFFFFF）；`pdMS_TO_TICKS` 用 64 位算，等待约 49.7 天，真卡死时会一直持 SD 卷锁、永远走不到下面的中止。所以 `filesystem_sd_bsp.c` 忽略 FatFs 传来的值，每个 DMA 分块一律等 `FILESYSTEM_FATFS_BSP_DMA_TIMEOUT_MS`（500 ms）。主机测试 `sd_removal_chain` 走真实的 `SD_read` → BSP 路径断言等待的 ticks 是 500。

SD 的 DMA 分块等完成信号量超过预算（500 ms）时，`filesystem_sd_transfer_finish` 调 `Platform_SD_AbortTransfer()`，不再向 Platform 提交 ERROR 事件：

| Platform 结果 | 含义 | Service 的动作 | 之后 |
|---|---|---|---|
| `STOPPED` | 传输已停止，卡回到 TRANSFER，Device 回到 READY | 清信号量和事件值，记 WARN，这一笔返回失败（FatFs 得到 `MSD_ERROR`） | 下一笔可以立即启动，不重新初始化 |
| `NO_MEDIA` | 中止时卡已拔出，SDMMC 已反初始化 | 同上 | Device 为 NOT_PRESENT |
| `STUCK` | 上界内没停住，SDMMC 已 `HAL_SD_DeInit()`，IRQ 节点已注销 | 同上 | 卷保持 ERROR，新的 `Start*` 被拒，不等待、不再中止；拔插卡或显式再 Init 才恢复 |
| `REJECTED` | 没有在飞的传输，或 Port 不支持中止 | 退回旧做法：向 Platform 提交 ERROR 事件，Device 进 ERROR | 同 `STUCK` |

错误事件、方向不符的事件和被拒的启动**不**触发中止：前者 HAL 在 ISR 里已经自己收了尾，后者硬件根本没启动。

**日志**（每次超时一行 WARN，持 SD 卷锁时投递：`Service_Log_Post` 不阻塞，可以调用）：

```
W (9850) FSABORT: stopped us=43 R blk=0 n=64
```

（上面是第 2 次上板钩子构建的真实一行。串口里每行的前缀是 `<时间> <I|W|E> (<tick>) <标签>: `，**标签后面有冒号**；本文下面写 `FSABORT stopped …`、`HOOK late_dma ON …` 都是省略前缀的简写，在日志里搜要带冒号：`FSABORT: stopped`、`HOOK: late_dma ON`、`SDHOOK: late_dma`。）标签 `FSABORT`，字段依次是结果（`stopped`、`no_media`、`stuck`、`rejected`）、`us=` 中止调用本身的耗时（周期计数器，微秒；计数器没启动时按 tick，毫秒精度）、`R`/`W` 方向、`blk=` 这一分块的起始块、`n=` 块数；最长约 50 个字符，放得进 Service Log 的 127 字符。一张卡持续超时时每次调用最多一行，两次之间至少隔一次完整的 500 ms 等待，不会刷屏。

**持锁上界。** 中止发生在持 SD 卷锁时，全程有界：等待 500 ms + 中止预算 100 ms（`SDCARD_ABORT_TIMEOUT_MS`，覆盖 CMD12、DPSM 静止、卡回到 TRANSFER 全部等待，每一步都带同一个截止点）+ 没停住时一次 `HAL_SD_DeInit()`。它进上面的“故障路径”行，与 R2.1 的关系：R2.1 的 20 ms 是**正常读**的上界，中止只在故障路径出现，不进 R2.1；500 ms 的等待加 100 ms 的中止预算仍小于播放侧的数据超时 T_data（现在 2 s）。**写方向**的预算偏紧（SDHC 写忙最长 250 ms，SDXC 500 ms）：产品路径不写 SD，写超时极少；万一卡在编程忙里超过 100 ms 没回到 TRANSFER，会走 `STUCK`，代价是重新插卡，不是数据损坏。

### 迟到 DMA 测试钩子（DIAG-7、#209）

`FILESYSTEM_HOOK_SD_LATE_DMA`（`filesystem_config.h`，默认 `0U`）是编译开关，代码写成 `if (FILESYSTEM_HOOK_SD_LATE_DMA != 0U)`，默认构建也编译，发布构建不开（CMake 在 Debug 以外的构建类型——Release、RelWithDebInfo、MinSizeRel——打开会直接报错）。打开方式：configure 时 `cmake --preset firmware-debug -DMP3_HOOK_SD_LATE_DMA=1`，关掉要显式 `-DMP3_HOOK_SD_LATE_DMA=0`（缓存变量留在构建目录里）。

- **启动日志**：第一次 `InitSD()` 投一行（WARN，只投一次），串口里是 `W (9835) HOOK: late_dma ON period=16 timeout=1ms`；没有 `HOOK:` 行就是没开（钩子关闭的构建里一行都不多）。它出现在第一次 `InitSD()` 的时刻，不在 `BOOT: FW` 旁边：第 2 次上板（那时 Storage Task 先挂载 Flash 再初始化 SD）的实测是开机后约 9.8 s，在 `FLASH: Filesystem mounted.` 之后、`SD: Card ready` 之前；**#211 之后第一次 `InitSD()` 在卷任务启动 SD 时，不再等 Flash 挂载，约在开机后 0.2 至 0.6 s、`SD: Card ready` 之后、`SD: Filesystem mounted.` 之前**（卷任务启动 SD，Flash 挂载在 Flash 启动任务里并行进行）；它不会被日志核心丢掉（日志核心只在调度器启动前的几百毫秒里丢最旧的几行，见 [Service Log](../log/README.md) 的“开机早期的丢行”）。其他钩子各自在所属模块首次初始化时同样投一行 `HOOK`。
- **做什么**：每 `FILESYSTEM_HOOK_SD_LATE_DMA_PERIOD`（16）次读请求探测一次，探测在调用者自己的读**之前**、复用 DMA 中转缓冲：① 读目标块（调用者这次读的起始块，至多 64 块），记 FNV-1a 校验和；② 从块 0 读 32 KiB 但只等 `FILESYSTEM_HOOK_SD_LATE_DMA_TIMEOUT_MS`（1 ms），32 KiB 平均要 3 ms 以上，必然超时并被显式中止（`FSABORT` 那一行照常出现）；③ **紧接着**再读目标块，校验和必须与 ① 一致——受害读（块 0 一带）与目标块内容不同，迟到的旧 DMA 若落进 bounce buffer、或旧的完成信号被下一笔当成完成，内容会变；目标块起始于 `FILESYSTEM_HOOK_SD_LATE_DMA_MIN_BLOCK`（64）之前（引导区一带）、以及所有写请求，都不探测。
- **结果行**（INFO，ERROR 见下；串口里形如 `I (9855) SDHOOK: late_dma n=1 blk=42657 victim=timeout verify=ok`）：`SDHOOK late_dma n=<探测序号> blk=<目标块> victim=<timeout|done|none> verify=<ok|MISMATCH|read_failed|skipped>`。判定：**通过**是 `victim=timeout verify=ok`，且前一行 `FSABORT` 是 `stopped`；`verify=MISMATCH`（ERROR）是迟到 DMA 或旧信号污染了紧接着的读，不通过；`verify=read_failed`（ERROR）是中止后紧接着的读失败，通常伴随 `FSABORT stuck`，卷保持 ERROR，按“恢复”一栏拔插卡；`victim=done` 是受害读居然在 1 ms 内完成，这次没有制造出迟到的 DMA，不算失败也不算证据；`verify=skipped`（WARN）是探测里第一次读就失败了。
- **代价**：每次探测给被探测的那次读加约 10–15 ms（估计，未测）且持 SD 卷锁；钩子构建的持锁统计不能当 R2.1 的证据。
- 主机测试 `filesystem_sd_late_dma_hook`（同一份测试和 Service 源码，另带 `-DFILESYSTEM_HOOK_SD_LATE_DMA=1` 编一份）覆盖启动行只投一次、节拍、受害读用 1 ms、紧接着的读校验、内容被改写时的 `MISMATCH`、`read_failed`、`skipped`、`victim=done`、低块号与写请求不探测；默认构建的 `filesystem_completion_wait` 断言钩子关闭时没有 `HOOK` 行。

### 磁盘状态与 STA_NOINIT

`BSP_USER_DISKIO_GetStatus` 只看 Platform 的卷状态是否就绪，不比较调用任务。旧实现里非所有者调用会得到 `STA_NOINIT`；`ff.c` 的 `find_volume` 对已挂载的卷在 `disk_status` 报 `STA_NOINIT` 时会清掉 `fs_type` 并走重新挂载（`ff.c` 第 3020 至 3036 行）。现在入口和磁盘状态都不再有任务身份检查，这条路径不存在。Flash 入口保留的是“InitFlash 已调用”的检查（与 SD 一致，由 `filesystem_handle` 的卷已初始化标志给出），它不看任务。

## 配置与资源

SD 保留 32 KiB、32 B 对齐的 AXI SRAM bounce buffer，配置在 `filesystem_config.h`。Flash 映射与工作内存由 Platform 注入 FTL，Service 不另建写回缓存。`flash/filesystem_flash_config.h` 控制等待预算和可选摘要日志，默认关闭日志不影响错误返回。

SD 与 Flash 初始化、挂载和错误状态独立。首版不提供跨任务文件请求队列、USB MSC 仲裁或原始 SD 块访问 Interface。当前产品范围不提供 USB MSC；批量文件导入使用读卡器。完整链路及验收记录见 [FTL 设计](../../docs/shape/storage.durability.md)、[公开接缝切开](../../docs/shape/storage.volumes.md) 与 [ADR-0014](../../docs/adr/0014-filesystem-volume-aware-file-interface.md)。
