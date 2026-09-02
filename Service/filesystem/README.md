# 文件系统 Service

本 Module 持有 SD/Flash FatFs 卷生命周期，在唯一 Storage Task 上下文执行同步块访问。仍是一个 Filesystem Module；`sd/` 和 `flash/` 是私有实现分区。公开 Interface 按调用者切开三个头，没有总揽头。卡检测、消抖、启动和回收时机由 APP 决定。

## 公开 Interface

- `filesystem_service.h`：卷生命周期。SD 为 `InitSD/MountSD/UnmountSD/FormatSD`；Flash 为 `InitFlash`（注册执行器）、`OpenFlash`（扫描 FTL）、`MountFlash/UnmountFlash`、`FormatFlash`、`ReclaimFlash`、`RecoverFlash`。
- `filesystem_file.h`：已挂载 Flash 卷上的单文件槽。`OpenFlashFile/ReadFile/WriteFile/SyncFile/CloseFile/RemoveFlashFile`。
- `filesystem_flash_access.h`：启动诊断的同步包装。`ReadFlashArray/ReadFlashDiagnostic/EraseFlashDiagnostic/ProgramFlashDiagnostic`。这是唯一允许包含 `platform_flash.h` 的公开头，以便使用 Platform 诊断区域枚举。

`FormatFlash` 显式注销卷、格式化 FTL、建立 FAT12/16 superfloppy，完成后不自动挂载；启动和挂载失败都不调用它。
`ReclaimFlash` 给 FTL 一次最多回收一块的机会，水位和目标仍归 FTL，可能什么都不擦。
`RecoverFlash` 注销旧卷、恢复硬件并重扫，不自动格式化或挂载，旧文件对象不可沿用。

卷、文件和诊断均返回 `Service_StatusTypeDef`。未格式化或无 FAT 返回 NO_FILESYSTEM，不完整/不兼容返回 NOT_READY，损坏/IO 错误返回 ERROR，执行器超时返回 TIMEOUT。FatFs 类型停留在本 Module 和中间件接缝内。

## 私有实现与编译依赖

`filesystem_status.c` 合并 FRESULT 映射与盘符路径转换。`sd/filesystem_sd_bsp.c` 强定义 BSP_SD_*，`sd/filesystem_sd_transfer.c/.h` 执行 SDMMC DMA。`flash/filesystem_flash_bsp.c` 强定义 BSP_USER_DISKIO_*，`flash/filesystem_flash_transfer.c/.h` 执行 Flash 请求并等待通知；诊断实现在 `flash/filesystem_flash_access.c`。

Service 包含 FatFs 卷对象/公开 API、中间件 BSP 契约、Platform SD/Flash 产品接口、原生 FreeRTOS 和 Log Service。FATFS 生成 Glue 只包含其契约，不包含 Service 头。FTL/W25Qxx/HAL 不作为 Service 的直接实现依赖。

## 请求与事件路径

FatFs disk_* → SD 或 USER BSP 强定义 → 私有执行器 → Platform。SDMMC/QSPI IRQ 经 Adapter 和 Platform 已注册回调唤醒同一 Storage Task。SD 使用通知索引 1，Flash 使用索引 2；索引 0 保留给 APP 卡检测。Flash 的唯一订阅和等待由本 Module 持有，诊断和逻辑请求共用所有者。APP 启动诊断直接调用 `filesystem_flash_access.h`，不再经过 APP 转发层。

通知只是唤醒提示，最终结果由普通上下文 Process 确认。ISR 不访问缓冲、不维护 Cache、不记录日志或提交下一笔操作。Flash 软件阶段主动让出调度，硬件阶段有界等待通知；超时后仍须完成安全收尾，控制器无法停止时不得提前返回并复用缓冲。

## 配置与资源

SD 保留 32 KiB、32 B 对齐的 AXI SRAM bounce buffer，配置在 `filesystem_config.h`。Flash 映射与工作内存由 Platform 注入 FTL，Service 不另建写回缓存。`flash/filesystem_flash_config.h` 控制等待预算和可选摘要日志，默认关闭日志不影响错误返回。

SD 与 Flash 初始化、挂载和错误状态独立。Flash 执行器拒绝其他任务，首版不提供跨任务文件请求队列、USB MSC 仲裁或原始 SD 块访问 Interface。当前产品范围不提供 USB MSC；批量文件导入使用读卡器。完整链路及验收记录见 [FTL 设计](../../docs/flash_ftl_design.md)、[公开接缝切开](../../docs/filesystem_service_reshape.md) 与 [ADR-0013](../../docs/adr/0013-usb-msc-product-scope.md)。

## Flash 文件访问

APP 只持有 `Service_Filesystem_FileTypeDef`，FIL、TCHAR、FRESULT 和 FatFs 调用均在
`flash/filesystem_flash_file.c` 中。首版仅支持已经显式挂载的 Flash 卷，同时一个打开文件，
接受最长 31 字节的根目录 ASCII 文件名，不接收卷号或路径分隔符；卷号取自 USERPath。
这是当前最小文件能力，不是跨任务文件队列，也不改变旧 SD benchmark。

打开方式为只读或仅新建；仅新建遇同名文件返回 SERVICE_BUSY，不截断或覆盖。
读写调用必须检查实际字节数：短读可能是 EOF，短写可能是容量不足，SERVICE_OK
不能代替长度检查。SyncFile 同步文件数据与目录元数据；CloseFile 成功后清零句柄。
所有函数均在唯一 Storage Task 同步执行，普通 APP 缓冲不直接交给 DMA。

Service 静态持有一个 FIL 及文件信息输出，避免挤占任务栈。句柄以代次校验，
关闭后重开或卷注销后旧句柄不可使用。挂载入口遇仍打开的文件返回 BUSY；正常注销前
调用者须先关闭文件。关闭失败保留槽及句柄，显式注销/恢复才丢弃旧对象，不伪造关闭成功。

RemoveFlashFile 只删除文件，不删除目录；文件不存在按成功处理，存在未关闭文件时拒绝删除。
无 TRIM 时，f_unlink 释放 FAT 簇并同步元数据，不把文件数据的 LBA 直接通知 FTL 作废。
后续 LBA 重写完成后，旧物理版本才成为可 GC 的失效块。文件删除不是安全擦除。
