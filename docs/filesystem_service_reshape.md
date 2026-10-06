# Filesystem Service 接缝与执行所有权

当前公开接口以 [Service README](../Service/filesystem/README.md) 和源码头文件为准；[ADR-0012](adr/0012-filesystem-public-seams.md) 记录接缝切开，[ADR-0014](adr/0014-filesystem-volume-aware-file-interface.md) 记录后续卷感知语义。旧迁移步骤与历史签名由 Git 保存。

## 当前接口分工

| 公开头 | 能力 |
| --- | --- |
| `filesystem_types.h` | Volume、UTF-8 相对路径和公共类型/上限 |
| `filesystem_service.h` | 独立 SD/Flash 初始化、卷生命周期与 Flash 回收/恢复 |
| `filesystem_file.h` | 卷感知文件和静态句柄槽 |
| `filesystem_directory.h` | 卷感知目录枚举 |
| `filesystem_flash_access.h` | 受限物理诊断；公开头中仅此头可依赖 Platform Flash 区域类型 |

没有总揽头，调用者按能力包含。FatFs、FreeRTOS 和 sd/flash 私有执行器类型不进入公开头。Filesystem 保持一个 Module，SD/Flash 独立执行器和状态，一个未就绪不使另一个伪报成功或失败。

## 执行与生命周期

Storage Task 是唯一同步执行上下文。APP 的 `sd/`、`flash/` 私有分区拥有启动、挂载策略和诊断时机；`storage_flash_init()` 是编排，不逐个转发 Service API。Service 持有唯一完成订阅、等待、FIL/DIR、盘符转换和句柄代次，APP 不直接含 FatFs。

InitSD 保存 APP 注入的 DMA 完成通知槽；再次挂载/卸载仍沿用该槽，不能默认成卡检测槽。Flash 物理诊断与逻辑请求复用同一执行者；原始诊断只用受限区域，不借自检 API 操作 FTL。

SD 没有设备侧 FormatSD。Flash 的 FormatAndMountFlash / RecoverAndMountFlash 成功返回时卷已挂载；文件/目录以 Volume + UTF-8 相对路径定位。当前类型、句柄失效、路径约束和错误语义见 Service README，避免在此复制签名。

Reclaim 给 FTL 一次最多回收一块的机会，水位/目标归 FTL，可能不擦任何块；内部 GC 操作码不表示调用一定回收。当前不建通用 BlockDevice、Service/storage 或跨任务请求队列，USB MSC 范围见 [ADR-0013](adr/0013-usb-msc-product-scope.md)。

## 私有状态转换

`filesystem_status.h/.c` 由本 Module 实现使用，共享 FRESULT 映射与盘符路径转换：

| `FRESULT` | `Service_StatusTypeDef` |
| --- | --- |
| `FR_OK` | `SERVICE_OK` |
| `FR_EXIST`、`FR_LOCKED` | `SERVICE_BUSY` |
| `FR_NOT_READY` | `SERVICE_NOT_READY` |
| `FR_TIMEOUT` | `SERVICE_TIMEOUT` |
| `FR_NO_FILESYSTEM` | `SERVICE_NO_FILESYSTEM` |
| `FR_INVALID_OBJECT`、`FR_INVALID_NAME`、`FR_INVALID_PARAMETER` | `SERVICE_INVALID_PARAM` |
| 其余 | `SERVICE_ERROR` |


物理诊断对非所有者或 timeout_ms 为零返回 SERVICE_NOT_READY；其他执行结束状态原样交给调用者，不压成 bool。轮询原始读取仍经 Platform 接口，异步执行由 Service 等待完成。

## 关联设计与验证

- [SD](sd_architecture.md)：插拔、消抖、DMA 与 BSP_SD Override。
- [FTL](flash_ftl_design.md)：USER DiskIO、恢复、掉电模型及同步完成。
- [文件基准](../APP/tasks/storage/benchmark/README.md)：图样、计时、失败清理与文件删除。
- [分层验证](verification.md)：软件回归与板级证据边界。
