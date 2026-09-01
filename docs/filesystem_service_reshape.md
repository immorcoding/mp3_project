# Filesystem Service 公开接缝切开

> 状态：已确认设计，待实现
> 日期：2026-09-01
> 相关决定：[ADR-0005](adr/0005-sd-filesystem-task-ownership.md)、[ADR-0010](adr/0010-fatfs-user-diskio-service-ownership.md)
> 实现后将新增 ADR-0012，记录本设计的长期取舍

## 1. 目的

Filesystem Module 仍然是一个 Module，SD/Flash 私有子目录保留。调整只切开公开 Interface，让调用者按需包含，不再把卷生命周期、Flash 文件和启动诊断挤在同一个头里。

行为不变：挂载/卸载/格式化语义、单文件槽、DMA/QSPI 执行器、DiskIO 强定义、Storage Task 唯一执行上下文均保持。

## 2. 已确认的取舍

1. 不新建 `Service/storage`，不抽通用 BlockDevice，不建 `filesystem_sd_access`。MSC 首版只导出 SD；所有权门槛和跨任务命令在 MSC 实现时加在现有 `BSP_SD_* → filesystem_sd_transfer_*` 之间。
2. 不做文件级卷选择。`ReadFile` / `WriteFile` 继续只服务已打开的 Flash 文件句柄；SD 没有文件 API。
3. 不把 Platform 诊断区域枚举复制成 Service 枚举。`filesystem_flash_access.h` 是唯一允许包含 `platform_flash.h` 的 Filesystem 公开头。
4. 删除 `APP/tasks/storage/storage_flash.c/.h`。`InitFlash` 由 `storage_task.c` 直接调用；诊断由 benchmark 直接调用 Service。
5. `Service_Filesystem_Init` 改名为 `Service_Filesystem_InitSD`，与 `InitFlash` 对称。
6. 自维护代码里所有 `Maintain` 公开符号一律改成 `Reclaim`（含 FTL / Platform / Service / APP）。它给 FTL 一次最多回收一块的机会，水位和目标仍归 FTL，可能什么都不擦。不用 `GarbageCollect`：太长，且暗示这次调用必定回收。FTL 内部操作码已是 `FLASH_FTL_OP_GC`，不必再改。

## 3. 目标目录

```text
Service/filesystem/
  filesystem_service.h              公开：卷生命周期
  filesystem_file.h                 公开：Flash 文件
  filesystem_flash_access.h         公开：四条启动诊断
  filesystem_status.h/.c            私有：FRESULT 映射与盘符路径
  filesystem_config.h               不变
  filesystem_service.c              仅卷生命周期
  sd/
    filesystem_sd_transfer.h/.c     不变
    filesystem_sd_bsp.c             不变
  flash/
    filesystem_flash_transfer.h/.c  瘦身：不再定义 Service_Filesystem_*
    filesystem_flash_access.c       四条诊断的 Implementation
    filesystem_flash_file.h/.c      实现改包含 filesystem_file.h
    filesystem_flash_bsp.c          不变
    filesystem_flash_config.h       不变

APP/tasks/storage/
  storage_task.c                    直接调用 InitFlash；去掉 storage_flash_init
  storage_sd.c                      Init → InitSD
  storage_flash.c/.h                删除
  benchmark/storage_flash_benchmark.c
                                    诊断改为 Service_Filesystem_*，比较 SERVICE_OK
  benchmark/storage_flash_file_benchmark.c
                                    改包含 filesystem_file.h
```

根 CMake 递归收集 `Service/*.c` 与 `APP/*.c`，新增 `.c` 自动进入固件，删除的 `storage_flash.c` 自动退出。不改 CMake 文件列表。

## 4. 包含规则

| 调用者 | 包含 | 禁止 |
| --- | --- | --- |
| `storage_sd.c`、`storage_task.c` | `filesystem_service.h` | 文件头、诊断头、`sd/` 与 `flash/` 私有头 |
| Flash 文件 benchmark、主机文件测试 | `filesystem_file.h` | 卷头、诊断头 |
| Flash 物理 benchmark | `filesystem_flash_access.h` | `storage_flash.h`、transfer 私有头 |
| `filesystem_sd_bsp.c` | `filesystem_sd_transfer.h`（保持） | 公开诊断头 |
| `filesystem_flash_bsp.c` | `filesystem_flash_transfer.h`（保持） | 公开诊断头 |
| 三个公开头 | `Service/service.h`；诊断头额外含 `platform_flash.h` | FatFs、FreeRTOS、私有 transfer 头 |

没有总揽头。谁需要哪类能力，就包含哪一个公开头。

## 5. 公开 Interface

### 5.1 卷生命周期 — `filesystem_service.h`

```c
Service_StatusTypeDef Service_Filesystem_InitSD(void);
Service_StatusTypeDef Service_Filesystem_FormatSD(void);
Service_StatusTypeDef Service_Filesystem_MountSD(void);
Service_StatusTypeDef Service_Filesystem_UnmountSD(void);

Service_StatusTypeDef Service_Filesystem_InitFlash(void);
Service_StatusTypeDef Service_Filesystem_OpenFlash(void);
Service_StatusTypeDef Service_Filesystem_MountFlash(void);
Service_StatusTypeDef Service_Filesystem_UnmountFlash(void);
Service_StatusTypeDef Service_Filesystem_FormatFlash(void);
Service_StatusTypeDef Service_Filesystem_ReclaimFlash(void);
Service_StatusTypeDef Service_Filesystem_RecoverFlash(void);
```

`InitSD` 的实现与现 `Init` 完全相同：检查 `retSD` / `SDPath`，再调用 `filesystem_sd_transfer_init()`。

`ReclaimFlash` 的实现与现 `MaintainFlash` 完全相同。自维护链路上的 `Maintain` 标识符一并改掉，不留旧名：

| 层 | 旧 | 新 |
| --- | --- | --- |
| Component | `FlashFTL_MaintainStart` | `FlashFTL_ReclaimStart` |
| Platform | `Platform_Flash_MaintainVolumeStart` | `Platform_Flash_ReclaimVolumeStart` |
| Platform | `PLATFORM_FLASH_VOLUME_OP_MAINTAIN` | `PLATFORM_FLASH_VOLUME_OP_RECLAIM` |
| Service | `Service_Filesystem_MaintainFlash` | `Service_Filesystem_ReclaimFlash` |
| Service | `filesystem_flash_transfer_maintain` | `filesystem_flash_transfer_reclaim` |
| Service | `FILESYSTEM_FLASH_MAINTAIN_TIMEOUT_MS` | `FILESYSTEM_FLASH_RECLAIM_TIMEOUT_MS` |
| APP | `STORAGE_FLASH_MAINTENANCE_PERIOD_MS` | `STORAGE_FLASH_RECLAIM_PERIOD_MS` |

主机测试 `Tests/flash_ftl/test_flash_ftl.c` 跟随 `FlashFTL_ReclaimStart`。文档里的「维护机会」改为「回收机会」。

### 5.2 Flash 文件 — `filesystem_file.h`

从现 `filesystem_service.h` 原样搬出：

- `SERVICE_FILESYSTEM_FILE_NAME_MAX_BYTES`
- `Service_Filesystem_FileTypeDef`
- `Service_Filesystem_FileModeTypeDef`
- `OpenFlashFile` / `ReadFile` / `WriteFile` / `SyncFile` / `CloseFile` / `RemoveFlashFile`

签名、单槽、代次、根目录 ASCII 名、`CREATE_NEW` 遇同名返回 `SERVICE_BUSY` 均不变。实现仍在 `flash/filesystem_flash_file.c`，改包含 `filesystem_file.h`。

### 5.3 启动诊断 — `filesystem_flash_access.h`

声明与现函数相同，返回值由 `bool` 改为 `Service_StatusTypeDef`：

```c
Service_StatusTypeDef Service_Filesystem_ReadFlashArray(uint32_t address,
                                                       uint8_t *data,
                                                       uint32_t data_length,
                                                       uint32_t timeout_ms);
Service_StatusTypeDef Service_Filesystem_ReadFlashDiagnostic(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint8_t *data,
    uint32_t data_length,
    uint32_t timeout_ms);
Service_StatusTypeDef Service_Filesystem_EraseFlashDiagnostic(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint32_t timeout_ms);
Service_StatusTypeDef Service_Filesystem_ProgramFlashDiagnostic(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint32_t page_offset,
    const uint8_t *data,
    uint32_t data_length,
    uint32_t timeout_ms);
```

实现从 `filesystem_flash_transfer.c` 搬到 `flash/filesystem_flash_access.c`。失败映射：

| 条件 | 返回 |
| --- | --- |
| 非所有者，或 `timeout_ms == 0` | `SERVICE_NOT_READY` |
| `filesystem_flash_transfer_finish()` 的结果 | 原样返回 |

成功路径不再把 `SERVICE_OK` 压成 `true`。`storage_flash_benchmark.c` 把 `if (!storage_flash_*(...))` 改为 `if (Service_Filesystem_*(...) != SERVICE_OK)`。轮询读仍走 `Platform_Flash_ReadArray()`，不经本头。

## 6. 私有共用状态转换 — `filesystem_status.h/.c`

合并现有两份 `FRESULT` 表。文件侧是卷侧的超集，合并后对卷调用无行为变化：

| `FRESULT` | `Service_StatusTypeDef` |
| --- | --- |
| `FR_OK` | `SERVICE_OK` |
| `FR_EXIST`、`FR_LOCKED` | `SERVICE_BUSY` |
| `FR_NOT_READY` | `SERVICE_NOT_READY` |
| `FR_TIMEOUT` | `SERVICE_TIMEOUT` |
| `FR_NO_FILESYSTEM` | `SERVICE_NO_FILESYSTEM` |
| `FR_INVALID_OBJECT`、`FR_INVALID_NAME`、`FR_INVALID_PARAMETER` | `SERVICE_INVALID_PARAM` |
| 其余 | `SERVICE_ERROR` |

`filesystem_make_drive_path()` 从 `filesystem_service.c` 原样迁入，供 `MountSD` / `UnmountSD` / `FormatSD` / `MountFlash` / `UnmountFlash` / `FormatFlash` 使用。该头仅供本 Module Implementation 包含。

`filesystem_flash_transfer.c` 不再包含 `filesystem_service.h`；它已通过 `filesystem_flash_transfer.h` 使用 `Service_StatusTypeDef` 与 Platform 类型。

## 7. APP 调用点

`storage_task()` 启动顺序改为：

```text
storage_sd_init(task_handle)
Service_Filesystem_InitFlash()          /* 取代 storage_flash_init */
可选 SDRAM / Flash 物理 benchmark
Service_Filesystem_OpenFlash()
Service_Filesystem_MountFlash()
可选 Flash 文件 benchmark
主循环：SD 消抖 + ReclaimFlash
```

`InitFlash` 失败时仍投递 `"Flash executor initialization failed."`，由 `storage_task.c` 写这条日志。

`storage_sd_prepare_filesystem()` 将 `Service_Filesystem_Init()` 改为 `Service_Filesystem_InitSD()`。

## 8. 迁移顺序

每一步结束后固件应能编译；行为在第 6 步之前与现网完全一致。

1. 新增 `filesystem_status.h/.c`，把映射和盘符路径从 `filesystem_service.c` 与 `filesystem_flash_file.c` 迁入；两边改为调用共用函数。
2. 新增 `filesystem_file.h`，从 `filesystem_service.h` 移出文件类型与六个文件函数声明；`filesystem_flash_file.c` 与文件测试/benchmark 改包含该头。
3. 新增 `filesystem_flash_access.h/.c`，把四条诊断从 `filesystem_flash_transfer.c` 迁出并改返回 `Service_StatusTypeDef`；`filesystem_service.h` 去掉诊断声明及其 `platform_flash.h`。
4. `Service_Filesystem_Init` 改名为 `InitSD`；按第 5.1 节把 FTL/Platform/Service/APP 的 `Maintain` 全部改为 `Reclaim`；更新对应测试与文档。
5. `storage_task.c` 直接调用 `InitFlash`；`storage_flash_benchmark.c` 改为调用 `Service_Filesystem_*`；删除 `storage_flash.c/.h`。
6. 同步 README、`CONTEXT.md`、架构文档与 ADR-0012。跑主机 Flash 文件测试；Debug 固件编译通过。

## 9. 文档同步范围

实现时必须改到与代码一致，不能只改源码：

| 文档 | 改什么 |
| --- | --- |
| `Service/filesystem/README.md` | 三个公开头、删除诊断在根头的描述、注明诊断头可含 Platform 区域类型 |
| `Service/README.md` | Filesystem 公开 Interface 不再写成单一挂载入口 |
| `APP/tasks/storage/README.md` | 删除 `storage_flash_*` 包装；InitFlash 与诊断调用点改写 |
| `APP/tasks/storage/benchmark/README.md` | MDMA/自检不再经 `storage_flash` 转发 |
| `CONTEXT.md` 文件系统 Module 段 | 把插在定义前的 Flash 文件段落并入正文；写明三个公开接缝；「维护机会」改为回收机会；相关术语补 **平台 Flash** |
| `docs/architecture_standard.md` §8 QSPI 路径、§13 | `storage_flash_init` → `Service_Filesystem_InitFlash`；诊断调用改为 Service；维护改为回收 |
| `docs/sd_architecture.md` | 目录表拆三个头；`Init` → `InitSD` |
| `docs/w25q256_architecture.md`、`docs/flash_ftl_design.md`、`Components/flash_ftl/README.md` | 去掉经 `storage_flash` 转发的表述；`Maintain*` → `Reclaim*` |
| `docs/adr/0012-filesystem-public-seams.md` | 新建：切开公开接缝、MSC 首版只导出 SD、本轮不建 storage/BlockDevice/sd_access、`Maintain` 全链改名 `Reclaim` |
| `docs/adr/README.md`、`docs/README.md` | 链到 0012 与本文 |

ADR-0005 / ADR-0010 不改写正文决定，由 0012 引用并收窄「未来 MSC」为「首版只导出 SD」。

## 10. 明确不做

- USB MSC 协议、LUN、跨任务命令队列、SD 所有者枚举
- SD 文件 API、通用 `Open(volume, path)`
- 合并 SD/Flash 执行器
- 改变 FTL 算法、`FLASH_FTL_OP_GC`、FatFs 生成 Glue
- 给诊断头再包一层 APP 转发

## 11. 验收

- Debug 与主机 `Tests/flash_ftl` 文件用例通过
- `filesystem_service.h` 不再声明文件或诊断函数，也不包含 `platform_flash.h`
- 自维护代码与文档无 `Service_Filesystem_Init(`、`FlashFTL_MaintainStart`、`MaintainVolumeStart`、`MaintainFlash`、`storage_flash_init`、`storage_flash_read_array` 等旧符号
- `storage_flash.c/.h` 不存在
- 三个公开头的包含关系符合第 4 节
