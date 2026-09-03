# 文件系统 Service

本 Module 持有 SD/Flash FatFs 卷生命周期，并在唯一 Storage Task 上下文执行同步块访问与卷感知文件/目录操作。仍是一个 Filesystem Module；`sd/` 和 `flash/` 是私有实现分区。公开 Interface 按调用者切开，没有总揽头。卡检测、消抖、启动和回收时机由 APP 决定。

当前文件与目录语义见 [ADR-0014](../../docs/adr/0014-filesystem-volume-aware-file-interface.md)。公开接缝切开与 `Reclaim` 命名见 [ADR-0012](../../docs/adr/0012-filesystem-public-seams.md)。当前产品不提供 USB MSC，见 [ADR-0013](../../docs/adr/0013-usb-msc-product-scope.md)。SD 为曲库主介质、Flash FTL 为机内盘与资源安装暂存，见 [ADR-0015](../../docs/adr/0015-volume-roles-and-resource-install.md)。

## 公开 Interface

- `filesystem_service.h`：卷生命周期。SD 为 `InitSD(notify_index)/MountSD/UnmountSD`；Flash 为 `InitFlash(notify_index)`（注册执行器）、`MountFlash`（扫描 FTL 并挂载 FAT）、`UnmountFlash`、`FormatAndMountFlash`、`RecoverAndMountFlash`、`ReclaimFlash`。设备不提供 SD 格式化入口。`notify_index` 是拥有该任务的 APP 枚举给出的通知槽，Service 不认识 APP 类型。
- `filesystem_file.h`：卷感知文件访问。`OpenFile/ReadFile/WriteFile/SeekFile/GetFilePosition/GetFileSize/SyncFile/CloseFile/RemoveFile/RenameFile/GetFileInfo`。调用必须给出 Volume；相对路径只在该卷内解释。
- `filesystem_directory.h`：卷感知目录访问。`OpenDirectory/ReadDirectory/RewindDirectory/CloseDirectory/CreateDirectory/RemoveDirectory`。空路径表示所选卷的根目录；文件接口不得把空路径当作文件名。
- `filesystem_flash_access.h`：启动诊断的同步包装。`ReadFlashArray/ReadFlashDiagnostic/EraseFlashDiagnostic/ProgramFlashDiagnostic`。这是唯一允许包含 `platform_flash.h` 的公开头，以便使用 Platform 诊断区域枚举。

`FormatAndMountFlash` 显式注销卷、格式化 FTL、建立 FAT12/16 superfloppy，成功返回时 Flash 文件系统必须已经重新挂载。启动和挂载失败都不调用它。
`RecoverAndMountFlash` 注销旧卷、恢复硬件并重扫，成功返回时卷已经挂载；旧文件/目录对象不可沿用。
`ReclaimFlash` 给 FTL 一次最多回收一块的机会，水位和目标仍归 FTL，可能什么都不擦。

卷、文件、目录和诊断均返回 `Service_StatusTypeDef`。未格式化或无 FAT 返回 `NO_FILESYSTEM`。卷未初始化或未挂载返回 `NOT_READY`。物理介质当时不可用（例如 SD 已拔出，FatFs 给出 `FR_NOT_READY`）返回 `NO_MEDIA`。容量不足返回 `NO_SPACE`，失效句柄返回 `INVALID_HANDLE`，损坏/IO 错误返回 `ERROR`，执行器超时返回 `TIMEOUT`。FatFs 类型停留在本 Module 和中间件接缝内。

## 路径契约

对外统一使用 UTF-8 相对路径，分隔符为正斜杠，例如 `Music/song.mp3`。禁止盘符、绝对路径、反斜杠、`.`、`..`、路径穿越、非法 UTF-8 以及超过 `SERVICE_FILESYSTEM_PATH_MAX_BYTES` 的路径。文件名和目录名不能为空。Service 内部为 SD/Flash 加上各自盘符，并转换成当前 FatFs TCHAR 编码。上层不能传入 `0:`、`1:`，也不能直接调用 `f_open`/`f_unlink`。

同名路径在两个卷上是彼此独立的对象。媒体条目必须保存来源卷和相对路径。首版播放列表只枚举 SD 的 `Music/`，不扫描 Flash，也不扫描卡根；`Update/` 等安装暂存路径不属于曲库。

## 文件与目录句柄

APP 只持有不透明 Token，`FIL`、`DIR`、TCHAR、FRESULT 和 FatFs 调用均在本 Module 内。静态文件槽与目录槽数量由 `filesystem_config.h` 的 `FILESYSTEM_FILE_SLOT_COUNT` / `FILESYSTEM_DIRECTORY_SLOT_COUNT` 给出，当前均为 4；用尽后打开返回 `SERVICE_BUSY`。句柄校验 Token、槽位、代次和所属 Volume。一卷卸载、SD 拔出、Flash 格式化或恢复后，该卷全部 Token 立即失效，返回 `SERVICE_INVALID_HANDLE`；另一卷的 Token 不受影响。

打开方式为只读、仅新建、读写已有文件或追加。仅新建遇同名文件返回 `SERVICE_BUSY`，不截断或覆盖。读写必须检查实际字节数：短读可能是 EOF，短写可能是容量不足，`SERVICE_OK` 不能代替长度检查。`WriteFile` 成功只表示本次数据已交给文件系统写入流程；`SyncFile` 对应 `f_sync`，返回成功前完成当前文件的 FatFs、FTL 和 Flash 写入语义。`CloseFile` 按值传递，成功后 Token 内部失效；关闭路径会执行必要同步，不得静默丢弃数据。

所有函数均在唯一 Storage Task 同步执行，普通 APP 缓冲不直接交给 DMA。首版没有跨任务文件请求队列；GUI 不得直接调用本同步接口。

`RemoveFile` 只删除文件，不删除目录；文件不存在按成功处理，目标仍打开时拒绝删除。无 TRIM 时，删除释放 FAT 簇并同步元数据，不把文件数据的 LBA 直接通知 FTL 作废。后续 LBA 重写完成后，旧物理版本才成为可 GC 的失效块。文件删除不是安全擦除。非空目录删除返回 `SERVICE_BUSY`。

## 私有实现与编译依赖

`filesystem_path.c` 校验 UTF-8 相对路径并加盘符转 TCHAR。`filesystem_handle.c` 管理静态 `FIL`/`DIR` 槽与代次 Token。`filesystem_file.c` / `filesystem_directory.c` 实现公开文件与目录 API。`filesystem_status.c` 合并 FRESULT 映射与盘符路径。`sd/filesystem_sd_bsp.c` 强定义 BSP_SD_*，`sd/filesystem_sd_transfer.c/.h` 执行 SDMMC DMA。`flash/filesystem_flash_bsp.c` 强定义 BSP_USER_DISKIO_*，`flash/filesystem_flash_transfer.c/.h` 执行 Flash 请求并等待通知；诊断实现在 `flash/filesystem_flash_access.c`。

Service 包含 FatFs 卷对象/公开 API、中间件 BSP 契约、Platform SD/Flash 产品接口、原生 FreeRTOS 和 Log Service。FATFS 生成 Glue 只包含其契约，不包含 Service 头。FTL/W25Qxx/HAL 不作为 Service 的直接实现依赖。

## 请求与事件路径

FatFs disk_* → SD 或 USER BSP 强定义 → 私有执行器 → Platform。SDMMC/QSPI IRQ 经 Adapter 和 Platform 已注册回调唤醒同一 Storage Task。SD DMA 完成槽与 Flash 操作槽在 `InitSD`/`InitFlash` 时由 APP 注入；卡检测槽只由 Storage Task 自己等待，不进入 Service。Flash 的唯一订阅和等待由本 Module 持有，诊断和逻辑请求共用所有者。APP 启动诊断直接调用 `filesystem_flash_access.h`，不再经过 APP 转发层。

通知只是唤醒提示，最终结果由普通上下文 Process 确认。ISR 不访问缓冲、不维护 Cache、不记录日志或提交下一笔操作。Flash 软件阶段主动让出调度，硬件阶段有界等待通知；超时后仍须完成安全收尾，控制器无法停止时不得提前返回并复用缓冲。

## 配置与资源

SD 保留 32 KiB、32 B 对齐的 AXI SRAM bounce buffer，配置在 `filesystem_config.h`。Flash 映射与工作内存由 Platform 注入 FTL，Service 不另建写回缓存。`flash/filesystem_flash_config.h` 控制等待预算和可选摘要日志，默认关闭日志不影响错误返回。

SD 与 Flash 初始化、挂载和错误状态独立。Flash 执行器拒绝其他任务，首版不提供跨任务文件请求队列、USB MSC 仲裁或原始 SD 块访问 Interface。当前产品范围不提供 USB MSC；批量文件导入使用读卡器。完整链路及验收记录见 [FTL 设计](../../docs/flash_ftl_design.md)、[公开接缝切开](../../docs/filesystem_service_reshape.md) 与 [ADR-0014](../../docs/adr/0014-filesystem-volume-aware-file-interface.md)。
