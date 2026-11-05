# 文件系统 Service

本 Module 持有 SD/Flash FatFs 卷生命周期，并同步执行块访问与卷感知文件/目录操作。完成通知不绑定任何任务（每卷一个静态二值信号量，见“请求与事件路径”）。文件接口是线程安全的：每卷一把卷锁在公开函数入口拿，另有一把只罩 `f_open`、`f_opendir` 和按定位符打开的窄表锁；GUI 与播放任务不得调用（入口断言停机）。规则与持锁上界见“并发模型”（ARC-21、[ADR-0024](../../docs/adr/0024-file-interface-concurrency.md)）。目前调用者仍只有 Storage Task，锁不会被争用；真实并发在后面的票才出现。仍是一个 Filesystem Module；`sd/` 和 `flash/` 是私有实现分区。公开 Interface 按调用者切开，没有总揽头；卷与路径上限在 `filesystem_types.h`，供文件、目录和生命周期头共用。卡检测、消抖、启动和回收时机由 APP 决定。

当前文件与目录语义见 [ADR-0014](../../docs/adr/0014-filesystem-volume-aware-file-interface.md)。公开接缝切开与 `Reclaim` 命名见 [ADR-0012](../../docs/adr/0012-filesystem-public-seams.md)。当前产品不提供 USB MSC，见 [ADR-0013](../../docs/adr/0013-usb-msc-product-scope.md)。SD 为曲库主介质、Flash FTL 为机内盘与资源安装暂存，见 [ADR-0015](../../docs/adr/0015-volume-roles-and-resource-install.md)。

## 公开 Interface

- `filesystem_types.h`：逻辑卷枚举与 UTF-8 路径/名称上限。文件头、目录头需要 Volume 或长度时只包含本头，互不包含。
- `filesystem_service.h`：卷生命周期。SD 为 `InitSD()/MountSD/UnmountSD`；Flash 为 `InitFlash()`（建立完成信号量并订阅）、`MountFlash`（扫描 FTL 并挂载 FAT）、`UnmountFlash`、`FormatAndMountFlash`、`RecoverAndMountFlash`、`ReclaimFlash`。设备不提供 SD 格式化入口。`Service_Filesystem_DenyCaller(TaskHandle_t)` 登记不得调用文件接口的任务（APP 启动时登记 GUI 与播放任务，见“并发模型”）。`InitSD()`/`InitFlash()` 没有参数：完成通知由 Service 自己的信号量承担，不借用任何任务的通知槽，Service 也不认识 APP 类型。两者各自须在任何任务使用文件接口之前调用一次（目前由 Storage Task 在启动时调用），调用时拿所属卷的卷锁；`InitSD()` 之后可以重复调用，只重新订阅传输事件，不重建信号量，挂载与卸载路径用它做幂等的就绪检查，`InitFlash()` 重复调用不做任何事。
- `filesystem_file.h`：卷感知文件访问。`OpenFile/ReadFile/WriteFile/SeekFile/GetFilePosition/GetFileSize/SyncFile/CloseFile/RemoveFile/RenameFile/GetFileInfo`。调用必须给出 Volume；相对路径只在该卷内解释。另有 `OpenFileByLocator`（按文件定位符只读打开，见下节“文件定位符与 FatFs 补丁”）。`ReadFile`/`WriteFile` 单次不超过 `SERVICE_FILESYSTEM_IO_MAX_BYTES`（32 KiB），超过返回 `SERVICE_INVALID_PARAM`，不截短也不在内部拆分。
- `filesystem_directory.h`：卷感知目录访问。`OpenDirectory/ReadDirectory/RewindDirectory/CloseDirectory/CreateDirectory/RemoveDirectory`。空路径表示所选卷的根目录；文件接口不得把空路径当作文件名。`ReadDirectory` 给出的目录项带文件定位符 `Locator`。
- `filesystem_text.h`：文本编码查表。`GbkToUnicode(gbk_code)` 把 GBK 双字节码（首字节在高 8 位）转 Unicode 码点，0 表示无映射；内部调用 FatFs `ff_convert(code, 1)`，只读 CP936 表（Resource Service 在任务启动前就绪），不访问介质、不阻塞，可在任何任务上下文调用。由 Storage 的 `storage_track_text_read()` 作为 GBK 查表回调注入曲目文本函数，FatFs 类型不出本 Module（ARC-12）。
- `filesystem_flash_access.h`：启动诊断的同步包装。`ReadFlashArray/ReadFlashDiagnostic/EraseFlashDiagnostic/ProgramFlashDiagnostic`。这是唯一允许包含 `platform_flash.h` 的公开头，以便使用 Platform 诊断区域枚举。

`FormatAndMountFlash` 显式注销卷、格式化 FTL、建立 FAT12/16 superfloppy，成功返回时 Flash 文件系统必须已经重新挂载。本 Service **不会**在启动或 `MountFlash` 失败时自行调用它。Storage Task 在 `SERVICE_NO_FILESYSTEM` 时是否调用，由 `STORAGE_FLASH_AUTO_FORMAT` 决定。
`RecoverAndMountFlash` 注销旧卷、恢复硬件并重扫，成功返回时卷已经挂载；旧文件/目录对象不可沿用。
`ReclaimFlash` 给 FTL 一次最多回收一块的机会，水位和目标仍归 FTL，可能什么都不擦。

卷、文件、目录和诊断均返回 `Service_StatusTypeDef`。未格式化或无 FAT 返回 `NO_FILESYSTEM`。卷未初始化或未挂载返回 `NOT_READY`。物理介质当时不可用（例如 SD 已拔出，FatFs 给出 `FR_NOT_READY`）返回 `NO_MEDIA`。容量不足返回 `NO_SPACE`，失效句柄返回 `INVALID_HANDLE`，损坏/IO 错误返回 `ERROR`，执行器超时返回 `TIMEOUT`，读写字节数超过 32 KiB 返回 `INVALID_PARAM`。FatFs 类型停留在本 Module 和中间件接缝内。

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
- **单次读写上限 32 KiB**（`SERVICE_FILESYSTEM_IO_MAX_BYTES`，与 SD 的 DMA 中转缓冲同大）：超过返回 `SERVICE_INVALID_PARAM`，不悄悄截短也不在内部拆分，这样一次读写的持锁时长有界。现有调用者都在上限以内：音频块 32 KiB（`STORAGE_AUDIO_BLOCK_BYTES`）、曲目标签块 4 KiB、音频探测头尾 10 B 与 160 B、SD 基准 32 KiB、Flash 文件基准 4 KiB 与一次 1 B 读回；这些常量在各自的源文件里有 `_Static_assert` 不超过上限。
- **调用者限制**：APP 启动时用 `Service_Filesystem_DenyCaller` 登记 GUI 与播放任务（至多 `FILESYSTEM_DENIED_CALLER_MAX` 个）。每个会等锁的公开函数（文件、目录、卷生命周期、Flash 诊断）第一件事是比较当前任务句柄，登记过的任务进来就 `configASSERT` 停机，Release 也保留，检查在等锁之前。`Service_Filesystem_GbkToUnicode` 只读 CP936 表、不拿锁，不受限制。用禁用名单是为了新增任务时共用层不用改。
- **死锁规则**：绝不同时持两把卷锁，跨卷操作由调用者分成多次调用；顺序固定卷锁 → 表锁；持文件锁时不调用会阻塞的接口（`Service_Log_Post` 不阻塞，可以调用）。同一任务重复拿同一把锁、持一把卷锁时再拿另一卷的锁、持表锁时再拿卷锁，都是 `configASSERT` 停机。
- **锁的创建**：三把锁在第一次用到时创建（临界区内幂等，静态存储），早于任何一次持锁，不要求哪个初始化函数先于哪个调用者。

### 其他共享状态的归属

| 状态 | 归属 |
|---|---|
| 文件槽、目录槽、卷的初始化与挂载标志、`SDFatFS`/`USERFatFS`、FatFs 的 `FatFs[]` 卷表项 | 所属卷的卷锁（槽按卷分区） |
| SD 完成信号量、事件值、DMA 中转缓冲 | SD 卷锁 |
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
| `MountFlash` | Flash | 3.3–8.3 s（8.3 s 的原因没查） | 只在启动时 |
| 故障路径 | 所属卷 | SD 单块 500 ms 超时；Flash 写预算 120 s | 不算正常上界 |

### 持锁统计

编译开关 `FILESYSTEM_LOCK_STATS_ENABLE`（`filesystem_config.h`）：固件 Debug 开（没有 `NDEBUG`），Release 关（带 `-DNDEBUG`，整段编译掉，不占时间也不占字段）。每把锁（SD、Flash、表）只记开机以来的最长持有、最长等待、各自对应的操作和拿取次数，用周期计数器量（微秒；区间超过 4 s 或计数器未启动时改用 tick，毫秒精度），不记逐次日志。

只出汇总日志，由放掉卷锁的那次公开调用顺带投：距上次汇总满 `FILESYSTEM_LOCK_STATS_PERIOD_MS`（10 s）就投一批，每把锁一行，共 3 行，**日志在锁放掉之后投，不在持锁时投递**；没有文件接口调用就没有汇总。格式（标签 `FSLOCK`，数值是开机以来的最大值，不清零）：

```
FSLOCK sd n=9 hold=4000us:Open wait=0us:-
FSLOCK flash n=7 hold=7000us:Reclaim wait=2500us:Info
FSLOCK table n=1 hold=4000us:Open wait=0us:-
```

`n` 是拿取次数；`hold`/`wait` 后面是最长一次的微秒数和对应的操作（持有的操作；等待的操作是等锁的那个调用），`-` 表示没有。操作名：`Open`、`OpenLoc`（按定位符）、`OpenDir`、`Read`、`Write`、`Seek`、`Tell`、`Size`、`Sync`、`Close`、`Remove`、`Rename`、`Info`、`ReadDir`、`Rewind`、`CloseDir`、`MkDir`、`RmDir`、`Init`、`Mount`、`Unmount`、`Format`、`Recover`、`Reclaim`、`FlashDiag`。每行最长约 80 个字符，远小于 Service Log 单条正文的 127 个字符。消息池核算见 [Service Log](../log/README.md)（DIAG-3）。

## 主机测试的保真边界

`Tests/flash_ftl`（`filesystem_lock_tests`、`filesystem_lock_release_tests`）与 `Tests/filesystem_locator` 经公开接口验证加锁，用共用 FreeRTOS 桩的互斥锁：可以脚本化“拿不到锁”“别的任务占着锁、等锁时放掉”，并读每把锁的拿放次数。通过证明：每次公开调用对所属卷的锁拿放各一次（成功和锁内的失败路径都一样）、另一卷的锁不碰；只有三个打开入口等表锁，且等表锁时只持自己的卷锁；槽按卷用尽返回 `SERVICE_BUSY`；32 KiB 上限；拒绝名单在等锁之前停机；重入同一卷、持一卷再进另一卷、持表锁再进卷入口都断言停机；统计的数字与格式，日志在锁放掉之后投，Release 构建不投。

**主机测试是单线程的，桩不切换任务也不真的阻塞，通过不等于并发没问题。** 不证明：真实的任务抢占与优先级继承、两卷同时进行的 DMA/QSPI、窄表锁在真实交错下的正确性（取决于上面的 `dec_lock` 写入顺序与 FatFs 内部行为）、持锁和等锁的真实时长、中断时序、栈余量。这些留给板级验收（#202 的 R2.x、R6.x）。

## 私有实现与编译依赖

`filesystem_path.c` 校验 UTF-8 相对路径并加盘符转 TCHAR。`filesystem_handle.c` 管理静态 `FIL`/`DIR` 槽（按卷分区）与代次 Token。`filesystem_lock.c/.h` 是卷锁、表锁、拒绝名单与持锁统计。`filesystem_file.c` / `filesystem_directory.c` 实现公开文件与目录 API。`filesystem_status.c` 合并 FRESULT 映射与盘符路径。`filesystem_file_locator.c` 实现按定位符打开，`filesystem_locator.h` 是定位符布局，`filesystem_ff_patch.h` 声明 `ff.c` 补丁函数。`sd/filesystem_sd_bsp.c` 强定义 BSP_SD_*，`sd/filesystem_sd_transfer.c/.h` 执行 SDMMC DMA。`flash/filesystem_flash_bsp.c` 强定义 BSP_USER_DISKIO_*，`flash/filesystem_flash_transfer.c/.h` 执行 Flash 请求并等待通知；诊断实现在 `flash/filesystem_flash_access.c`。

Service 包含 FatFs 卷对象/公开 API、中间件 BSP 契约、Platform SD/Flash 产品接口、原生 FreeRTOS 和 Log Service。FATFS 生成 Glue 只包含其契约，不包含 Service 头。FTL/W25Qxx/HAL 不作为 Service 的直接实现依赖。

## 请求与事件路径

FatFs disk_* → SD 或 USER BSP 强定义 → 私有执行器 → Platform。执行器等待**每卷一个静态二值信号量**（SD 一个、Flash 一个，[ADR-0024](../../docs/adr/0024-file-interface-concurrency.md)）：正在执行传输的任务（调用者，不绑定固定任务）等待，SDMMC/QSPI 中断经 Adapter 和 Platform 已注册的回调给出。卡检测由 APP 自己处理，不进入 Service。Flash 的唯一订阅和等待由本 Module 持有，诊断和逻辑请求共用同一个信号量。APP 启动诊断直接调用 `filesystem_flash_access.h`，不再经过 APP 转发层。

通知只是唤醒提示，最终结果由普通上下文 Process 确认。ISR 不访问缓冲、不维护 Cache、不记录日志或提交下一笔操作。Flash 软件阶段主动让出调度，硬件阶段有界等待信号量；超时后仍须完成安全收尾，控制器无法停止时不得提前返回并复用缓冲。

### 完成来源

- **SD**：SDMMC1 中断 → STM32 SDMMC IRQ Adapter → Platform SD 的传输回调（`InitSD` 经 `Platform_SD_SetTransferCallback` 订阅）→ SD 执行器的回调：先把事件值（读完成、写完成、错误、中止）写进一个 `volatile` 变量，再 `xSemaphoreGiveFromISR`。任务拿到信号量后才读事件值并交给 `Platform_SD_CompleteTransfer()`。同一次等待内后到的事件覆盖先到的（与原先覆盖式通知一致）。
- **Flash**：QSPI/MDMA 中断 → STM32 QSPI IRQ Adapter → Platform Flash 的操作回调（`InitFlash` 经 `Platform_Flash_SetOperationCallback` 订阅）→ Flash 执行器的回调：只 `xSemaphoreGiveFromISR`，不带事件值。信号只是提示，结果以 `Platform_Flash_ProcessOperation()` 为准，且每 `FILESYSTEM_FLASH_WAIT_SLICE_MS` 重查一次，所以信号被合并或丢失也只是晚一个周期。
- 两个回调都只用 `FromISR` 版本，并把 `higher_priority_task_woken` 交给 `portYIELD_FROM_ISR`，不做别的事。回调对应的中断优先级必须满足 `configMAX_SYSCALL_INTERRUPT_PRIORITY`（与改用信号量之前调用 `xTaskNotifyIndexedFromISR` 的要求相同）。

### 信号量何时创建

静态存储（`StaticSemaphore_t`），不占堆。句柄在第一次 `InitSD()` / `InitFlash()` 里创建，**先创建、后向 Platform 订阅回调**：Platform 只在订阅之后才会调用 ISR 回调（SD 的传输中断节点要在回调存在且卡就绪时才安装；Flash 的 `Start*` 在回调未设置时直接拒绝，没有在飞操作就没有中断事件），而且没有 `Start*` 就没有完成中断，所以任何一次完成中断到来时信号量都已经存在。初始化之前调用传输函数得到失败（SD 返回 `false`，Flash 返回 `SERVICE_NOT_READY`），不会去等一个不存在的信号量。`InitSD()`/`InitFlash()` 在所属卷的卷锁内调用，所以第一次调用不与别的调用并发。

### 迟到的旧信号

超时之后才到达的中断会让信号量保持置位，没有任务在等。处理方式：

- **清理时机**：每次**启动传输之前**（SD：每个 DMA 分块的 `Platform_SD_Start*Blocks` 之前；Flash：`filesystem_flash_transfer_finish` 进入时，此时 Start 已经发生但还没有第一次 `Process`）用 `xSemaphoreTake(…, 0)` 清掉。SD 不能在启动之后清，否则会丢掉本次传输自己的中断；Flash 可以在 Start 之后清，因为第一次 `Process` 才是权威，清理之前到达的完成已经体现在 Platform 状态里。
- **SD 没有被清掉的窗口**：旧 DMA 在“清理之后、本次启动之前”才完成，这条路径清不到，也无法靠信号量区分（中断说不出是哪次传输结束的）。实际上超时会让 Device 进 `ERROR`，`Start*` 在 `ERROR` 下被拒绝，要等卡重新初始化才能再传，所以这个窗口难以构造（#188 也没有构造出来）；彻底解决靠有上界的显式 Abort（ADR-0024 第 7 条，#209 的范围），本票不改。
- **Flash 不会返回后还有 DMA 在飞**：超时只触发 `Abort`，执行器继续 `Process` 直到控制器静止才返回，所以返回之后到的信号最多是最后一次完成中断没被取走；它被下一次进入时清掉，即使没清，最坏也只是第一次等待提前醒来一次，`Process` 仍报忙就继续等。

### 磁盘状态与 STA_NOINIT

`BSP_USER_DISKIO_GetStatus` 只看 Platform 的卷状态是否就绪，不比较调用任务。旧实现里非所有者调用会得到 `STA_NOINIT`；`ff.c` 的 `find_volume` 对已挂载的卷在 `disk_status` 报 `STA_NOINIT` 时会清掉 `fs_type` 并走重新挂载（`ff.c` 第 3020 至 3036 行）。现在入口和磁盘状态都不再有任务身份检查，这条路径不存在。Flash 入口保留的是“InitFlash 已调用”的检查（与 SD 一致，由 `filesystem_handle` 的卷已初始化标志给出），它不看任务。

## 配置与资源

SD 保留 32 KiB、32 B 对齐的 AXI SRAM bounce buffer，配置在 `filesystem_config.h`。Flash 映射与工作内存由 Platform 注入 FTL，Service 不另建写回缓存。`flash/filesystem_flash_config.h` 控制等待预算和可选摘要日志，默认关闭日志不影响错误返回。

SD 与 Flash 初始化、挂载和错误状态独立。首版不提供跨任务文件请求队列、USB MSC 仲裁或原始 SD 块访问 Interface。当前产品范围不提供 USB MSC；批量文件导入使用读卡器。完整链路及验收记录见 [FTL 设计](../../docs/shape/storage.durability.md)、[公开接缝切开](../../docs/shape/storage.volumes.md) 与 [ADR-0014](../../docs/adr/0014-filesystem-volume-aware-file-interface.md)。
