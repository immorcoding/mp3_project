# 文件系统 Service

本 Module 持有 SD/Flash FatFs 卷生命周期，在唯一 Storage Task 上下文执行同步块访问。仍是一个 Filesystem Module；`sd/` 和 `flash/` 是私有实现分区，根公开头统一对外。卡检测、消抖、启动和维护时机由 APP 决定。

## 公开 Interface

- SD：`Service_Filesystem_Init/MountSD/UnmountSD/FormatSD`，原有行为保持。
- Flash：`InitFlash` 注册执行器；`OpenFlash` 扫描 FTL；`MountFlash/UnmountFlash` 管理 FAT 卷。
- `FormatFlash` 显式注销卷、格式化 FTL、建立 FAT12/16 superfloppy，完成后不自动挂载；启动和挂载失败都不调用它。
- `MaintainFlash` 提供一次维护机会；`RecoverFlash` 注销旧卷、恢复硬件并重扫，不自动格式化或挂载，旧文件对象不可沿用。
- `ReadFlashArray/ReadFlashDiagnostic/EraseFlashDiagnostic/ProgramFlashDiagnostic` 是启动诊断的同步包装，不用于逻辑卷。

卷流程返回 `Service_StatusTypeDef`；诊断包装返回 bool。未格式化或无 FAT 返回 NO_FILESYSTEM，不完整/不兼容返回 NOT_READY，损坏/IO 错误返回 ERROR，执行器超时返回 TIMEOUT。FatFs 类型停留在本 Module 和中间件接缝内。

## 私有实现与编译依赖

`sd/filesystem_sd_bsp.c` 强定义 BSP_SD_*，`sd/filesystem_sd_transfer.c/.h` 执行 SDMMC DMA；迁移不改变其传输语义。`flash/filesystem_flash_bsp.c` 强定义 BSP_USER_DISKIO_*，`flash/filesystem_flash_transfer.c/.h` 执行 Flash 请求并等待通知。

Service 包含 FatFs 卷对象/公开 API、中间件 BSP 契约、Platform SD/Flash 产品接口、原生 FreeRTOS 和 Log Service。FATFS 生成 Glue 只包含其契约，不包含 Service 头。FTL/W25Qxx/HAL 不作为 Service 的直接实现依赖。

## 请求与事件路径

FatFs disk_* → SD 或 USER BSP 强定义 → 私有执行器 → Platform。SDMMC/QSPI IRQ 经 Adapter 和 Platform 已注册回调唤醒同一 Storage Task。SD 使用通知索引 1，Flash 使用索引 2；索引 0 保留给 APP 卡检测。Flash 的唯一订阅和等待已从 APP storage_flash 迁入本 Module，诊断和逻辑请求共用所有者。

通知只是唤醒提示，最终结果由普通上下文 Process 确认。ISR 不访问缓冲、不维护 Cache、不记录日志或提交下一笔操作。Flash 软件阶段主动让出调度，硬件阶段有界等待通知；超时后仍须完成安全收尾，控制器无法停止时不得提前返回并复用缓冲。

## 配置与资源

SD 保留 32 KiB、32 B 对齐的 AXI SRAM bounce buffer，配置在 `filesystem_config.h`。Flash 映射与工作内存由 Platform 注入 FTL，Service 不另建写回缓存。`flash/filesystem_flash_config.h` 控制等待预算和可选摘要日志，默认关闭日志不影响错误返回。

SD 与 Flash 初始化、挂载和错误状态独立。Flash 执行器拒绝其他任务，首版不提供跨任务文件请求队列或 USB MSC 仲裁。完整链路及验收记录见 [FTL 设计](../../docs/flash_ftl_design.md)。
