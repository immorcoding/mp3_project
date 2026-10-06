# Storage · 卷与执行接缝参考

[Storage](storage.md) 的技术参考；所有权按 [architecture](architecture.md) ARC-11/12，公开头、Token、UTF-8 路径和返回值以 [Filesystem README](../../Service/filesystem/README.md) 为准。

## 卷生命周期

SD 与 Flash 独立初始化、挂载和报告错误。Storage Task 决定时机，Service 持有 FIL/DIR、盘符转换和静态句柄代次；Flash 原始诊断和逻辑请求共用同一个执行者。APP 不能抢占完成回调。Init 注入的 SD/Flash 通知槽在后续挂载/卸载继续沿用，不能退回卡检测事件槽。

Flash 启动先使 SDRAM/QSPI/W25Qxx 就绪，再绑定分区和内存、注册 Service 执行器、扫描 FTL，最后按 USERPath 挂载。破坏性 SDRAM 自检必须早于 FTL 表与业务缓冲使用；USER 初始化遇已就绪后端不重复扫描。

SD 不提供设备侧格式化。Flash `FormatAndMountFlash` 先注销旧 FAT 卷、格式化 FTL、用 `FM_FAT | FM_SFD` 建 FAT12/16 并挂载；`RecoverAndMountFlash` 先注销旧对象、安全恢复硬件、重扫再挂载。二者只有已挂载才返回成功，旧 Token 不可继续用。Reclaim 是一次最多一块的回收机会，可能不擦任何块，水位归 FTL。

FTL 和 Service 的普通挂载不自动格式化。当前 APP 在 `MountFlash` 返回 `SERVICE_NO_FILESYSTEM` 且 `STORAGE_FLASH_AUTO_FORMAT` 启用时显式调用格式化入口；其他错误不触发格式化。开关现值查 [storage_task_config.h](../../APP/tasks/storage/storage_task_config.h)，策略查 [storage_flash.c](../../APP/tasks/storage/flash/storage_flash.c)。这是上层策略与底层安全默认的区别，不把旧设计的笼统“启动不格式化”当成现状。

## FatFs 入站接缝

请求为 `FatFs disk_* → BSP 强定义 → Service 私有执行器 → Platform → SD 或 FTL`。生成 Glue 只认识 BSP 契约。USER 后端的弱默认以 `STA_NOINIT/RES_NOTRDY` 安全失败；强定义须实际进入链接目标，不能仅依赖静态库可能不被提取的对象。CubeMX 重生成后检查 USER CODE 薄转发与最终 map。

SDPath/USERPath 来自各自 Driver Link 的结果；全局盘符和驱动内 LUN 是不同概念，USER LUN 为 0。固定逻辑扇区为 512 B，`GET_SECTOR_COUNT` 取实际 FTL 几何；`GET_BLOCK_SIZE=1` 满足当前 FatFs 二次幂要求，不把 FTL 七扇区组当擦除提示。`CTRL_SYNC` 经同步执行器，未知命令报 `RES_PARERR`、未就绪报 `RES_NOTRDY`、传输错误报 `RES_ERROR`。

## 同步、删除与诊断

文件 `Write` 和 `Sync` 都可能产生真实物理写入；同步接口阻塞调用任务时 RTOS 仍可运行其他任务。只收到通知不意味着成功，普通上下文完成 Process/Cache 收尾才得最终结果。

文件删除释放 FAT 簇而非安全擦除。当前 TRIM 关闭，只有对应 LBA 重写并提交后，旧数据版本才可 GC；目录/FAT 元数据更新不等于 payload 已作废（ARC-12）。文件基准仅新建自己的测试文件、读回校验后删除；同名不覆盖，异常清理失败明确报告残留，未挂载则跳过。图样与计时看 [benchmark README](../../APP/tasks/storage/benchmark/README.md)。

受限物理诊断对非所有者或零超时返回 NOT_READY，其他结果完整返回，不能压成 bool。私有 FRESULT 映射查 [filesystem_status.c](../../Service/filesystem/filesystem_status.c)，不在文档复制容易落后的状态表。公开文件接口还区分 NO_MEDIA、NO_SPACE、INVALID_HANDLE，不把它们与私有通用映射混为一谈。

接缝理由见 [ADR-0010](../adr/0010-fatfs-user-diskio-service-ownership.md)、[ADR-0012](../adr/0012-filesystem-public-seams.md)、[ADR-0014](../adr/0014-filesystem-volume-aware-file-interface.md)。
