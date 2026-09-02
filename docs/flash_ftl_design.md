# Flash FTL 首版设计与实施清单

> 状态：首版代码已实现；主机回归、Debug/Release 构建通过，硬件验收待完成。
> 日期：2026-08-31。
> 授权记录：先完成文档，后经用户“依据文档开始代码部分”授权实施，并采用 TDD；未执行硬件烧录、设备格式化或 Git 提交。
> 相关决定：[ADR-0010](adr/0010-fatfs-user-diskio-service-ownership.md)、[ADR-0011](adr/0011-ftl-copy-on-write-and-recovery.md)。
> 原始 NOR 现状见 [w25q256_architecture.md](w25q256_architecture.md)，规范见 [architecture_standard.md](architecture_standard.md) 和 [coding_standard.md](coding_standard.md)。

## 1. 范围与代码现状

首版拉通 Filesystem Service 及以下的 Flash 逻辑扇区链路，并安排 Storage Task 的启动、回收和恢复入口。暂不创建通用 BlockDevice、独立 Service/storage、资源包在线更新或 GUI/模型加载流程；USB MSC 不属于当前产品范围。

当前 W25Qxx 已有读取、单页编程、4 KiB 擦除和 WIP 自动状态轮询；QSPI/MDMA 读取已接入。页编程的数据发送当前仍使用 HAL 轮询，不能声称已支持 MDMA TX。FTL、Bridge、Platform 装配、Service 同步执行器和 USER 转发均已接通；Storage Task 已安排打开/挂载及定期回收。

SD 路径仍经过 SDCard Component；卡内部控制器完成底层 Flash 映射，不能据此省略原始 NOR 所需的 FTL。

## 2. 三类关系与分层职责

### 2.1 所有权

| Module / 层 | 职责 | 不承担 |
| --- | --- | --- |
| FatFs / FATFS Glue | 文件系统、卷链接、DiskIO 与外部 BSP 契约 | FTL、任务等待、板级装配 |
| Filesystem Service | BSP 强定义、同步执行器、错误转换、可选日志、显式卷流程 | 映射、芯片协议 |
| Storage Task | 唯一执行上下文、启动/挂载策略、回收时机 | QSPI 实现、GC 算法 |
| Platform Flash | 长期持有实例/内存、绑定分区、管理映射模式 | FTL 元数据解释、FatFs、任务等待 |
| Flash FTL Component | 映射、提交、扫描、分配、GC、RawOps 契约 | W25Qxx、HAL、RTOS、文件系统 |
| FTL W25Qxx Bridge | 地址/范围及两个 Component 的操作和状态转换 | 任务、GC、HAL |
| W25Qxx / HAL Adapter | 芯片命令生命周期 / 控制器、DMA、Cache、IRQ 接缝 | 文件、逻辑扇区映射 |

### 2.2 编译依赖

- FTL 只依赖 ISO C、本 Module 类型和注入的 `FlashFTL_RawOps`，不包含 W25Qxx、Platform、HAL、FreeRTOS、FatFs 头。
- Bridge 包含 FTL 与 W25Qxx 公开头；Platform 包含两者及 Adapter 的装配 Interface。
- FATFS Glue 只包含自己声明的 BSP 契约，不包含 Service 头；Service 包含契约并提供强定义。
- 可调宏放所属 Module 私有 `_config.h`。“公开宏供维护者调整”不等于允许其他 Module 包含私有配置头。

### 2.3 运行路径

```text
StorageTask 调用 Service 文件流程
  -> FatFs f_* -> disk_* -> USER_diskio
  -> BSP_USER_DISKIO_* 契约的 Service 强定义
  -> Filesystem Flash 同步执行器
  -> Platform Flash -> Flash FTL
  -> FlashFTL_RawOps -> FTL W25Qxx Bridge
  -> W25Qxx -> BusOps -> STM32 HAL QSPI Adapter -> Flash

硬件 IRQ -> IRQ Adapter -> Platform 已注册回调
  -> Service 私有执行器通知 StorageTask 索引 2
  -> 普通任务继续 Process、Cache 收尾及下一步操作
```

ISR 只记录结果并通知，不访问数据缓冲、不记日志、不推进 FTL、不提交下一页。索引 0 保留 SD 检测，索引 1 保留 SD 传输，索引 2 用于 Flash。Filesystem Service Flash 私有执行器长期持有唯一订阅和等待；原始诊断与 FTL 共用执行所有者，不临时抢占回调。

## 3. CubeMX 与 USER DiskIO 契约

H743 当前 CubeMX User-defined 配置只生成 `user_diskio.c/.h` 骨架，不自动生成等同 SD BSP 的 Flash 后端。保留生成文件，只在 USER CODE 区接入转发。

当前自维护 `FATFS/Target/bsp_driver_user_diskio.c/.h`：

- `BSP_USER_DISKIO_Init`、`BSP_USER_DISKIO_GetStatus`；
- `BSP_USER_DISKIO_ReadBlocks`、`BSP_USER_DISKIO_WriteBlocks`、`BSP_USER_DISKIO_Ioctl`。

头声明契约，`.c` 提供安全弱默认实现，Service 提供同名强定义。弱属性只在弱定义处使用，不污染声明或强定义。缺少后端时报告未就绪，读写不得伪报成功。`DSTATUS`、`DRESULT` 等 FatFs 类型不进入 Platform/FTL。

当前 USER 读写无实际操作却返回 `RES_OK`，接入时必须替换。让强定义源直接进入构建目标并检查最终链接 map；仅放入未被提取的静态库不能保证接管。CubeMX 重生成后检查 USER CODE 保留和源文件纳入情况。

当前两个卷、固定 512 B 扇区、无多分区、TRIM 关闭。先链接 SD 再链接 USER，两次成功时通常分别得到 `0:/` 和 `1:/`；使用 `SDPath`、`USERPath` 与各自链接结果，不硬编码卷号。传给 USER 函数的是驱动内 LUN，当前为 0，不能与全局卷号 1 比较。

Ioctl 提供同步与准确的逻辑容量/扇区信息。擦除提示的 FatFs 约束、完整返回值映射在接口落定时核对当前库；不能把每组七扇区直接作为未经核验的 FatFs 擦除提示。首版不增加 TRIM。

## 4. 分区、容量和可调宏

Platform 注入分区基址和长度；FTL 使用分区内相对偏移；Bridge 校验完整范围后转芯片绝对地址，检查溢出、页/块对齐及跨分区访问。格式化和 GC 也不得绕过检查。

[ADR-0009](adr/0009-w25q256-firmware-slots-and-diagnostic-reservation.md) 保留区不变：

| 物理范围（含端点） | 用途 |
| --- | --- |
| `0x00000000–0x00000FFF` | 首自检扇区 |
| `0x00001000–0x00200FFF` | 2 MiB rollback 槽 |
| `0x00201000–0x00400FFF` | 2 MiB candidate 槽 |
| `0x00401000–0x01FFEFFF` | 原始资源预留与 FTL 候选区 |
| `0x01FFF000–0x01FFFFFF` | 尾自检扇区 |

物理容量初始可在 16–24 MiB 调整，默认 24 MiB。可按排他上界 `0x01FFF000` 向低地址计算：24 MiB 示例基址为 `0x007FF000`，低地址资源保留为 `0x00401000–0x007FEFFF`。此示例待装配核验，未烧录、未格式化。首次破坏性操作前核验资源预留边界、已有内容与不重叠关系，不要求先实现资源包或 Bootloader 协议。

| 所属配置 | 宏 | 默认值与含义 |
| --- | --- | --- |
| Platform Flash | `PLATFORM_FLASH_FTL_SIZE_BYTES` | `24UL * 1024UL * 1024UL`，物理分区字节数 |
| FTL | `FLASH_FTL_RESERVE_PERCENT` | `10U`，扣除双卷头后的数据物理块数为分母 |
| FTL | `FLASH_FTL_GC_START_RESERVE_PERCENT` | `75U`，预留块预算为分母 |
| FTL | `FLASH_FTL_GC_STOP_RESERVE_PERCENT` | `90U`，预留块预算为分母 |
| FTL | `FLASH_FTL_MIN_FREE_BLOCKS` | `2U`，保护下限，不是 GC 启动水位 |
| Filesystem Flash | `FILESYSTEM_FLASH_DIAG_LOG_ENABLE` | `0U`，关闭可选诊断 |

`N = 物理容量 / 4096`，`D = N - 2`，`R = ceil(D * 10 / 100)`，逻辑组数 `G = D - R`，扇区数 `S = G * 7`。百分比向上取整，计算防溢出。

24 MiB：6144 物理块，6142 数据物理块，615 预留块，5527 逻辑组，38689 个 512 B 扇区，可见容量 19,808,768 B，约 18.9 MiB。16 MiB 对应约 12.6 MiB。尚未扣除 FAT 文件系统开销。

容量/预留比例改变卷几何，必须与卷头核对，不能只改宏就重解释已有盘；GC 水位只改变策略。配置注释明确单位、分母、取整、触发方向和相互约束。

## 5. 逻辑组与记录格式

扇区 512 B，七个扇区组成 3584 B 逻辑组：`group = LBA / 7`，`offset = LBA % 7`。一个组版本独占一个 4096 B 擦除块。

| 块内偏移 | 长度 | 内容 |
| --- | ---: | --- |
| `0x000–0x0FF` | 256 B | 记录头 |
| `0x100–0xEFF` | 3584 B | 七扇区，十四个编程页 |
| `0xF00–0xFFF` | 256 B | 最后写入的提交记录 |

头包含 magic、格式版本、格式代次 epoch、组号、64 位组版本、七个扇区各自 CRC32、头 CRC32。提交包含 magic、格式版本、epoch、组号、组版本、绑定头的 CRC、提交自身 CRC。规定字节序并显式编解码，不直接写 C 结构体。具体偏移、CRC 参数/覆盖范围和保留字节在实现前落定，不冒充已定稿二进制协议。

每组只映射一个当前有效块，相同逻辑容量下条目数为逐 512 B 映射的七分之一。布局效率 87.5%，还需扣除卷头和预留块。

未映射扇区读出 `0xFF`，RAM 填充即可；首次局部写入同组未写部分也填 `0xFF`。已有组的其他有效数据必须保留。新块已确认全块擦除时，全 `0xFF` 数据页可以跳过编程，但仍参与 CRC 和读回校验；不减少必要擦除，不改变 4 KiB 分配单位。

## 6. 读写与同步完成

一次组更新：

1. 检查请求，准备 4 KiB 工作区；局部更新读旧组，整组覆盖可省略旧读取。同一请求对同组的更新合并处理。
2. 分配已确认擦除的新块；不足先推进 GC，不能擦除当前有效块腾空间。
3. 写头和必要数据页，每页在本擦除周期最多编程一次。
4. 读回校验头和完整数据，包含跳过编程的 `0xFF` 页。
5. 最后写提交页，等待完成并读回验证。
6. 更新 RAM 映射，再标旧块可回收，此后才报告该组完成。

不回写旧块失效标记。读取只返回当前映射数据，校验失败不静默回退。首版无“仅写入 RAM 就返回成功”的 FTL 写回缓存。

跨组请求逐组提交，不保证整个请求原子，失败前部分组可能已提交。未确认请求掉电后可恢复为旧版或完整提交的新版；已确认数据须满足定义故障模型下的持久性。单组原子性不等于 FatFs 跨组/整文件事务原子性。

`f_write()` 和 `f_sync()` 都可能触发真实写入；后者提交 FatFs 缓冲/元数据并调用 `CTRL_SYNC`。FTL Sync 确认先前写入完成和硬件安全，不要求清空所有 GC 工作。不能把耗时固定归于 `f_sync()`。

Service 同步接口阻塞当前任务等待，RTOS 可调度其他任务；FTL Process 不等待通知。总耗时含多页编程、校验读、必要 GC、调度，不能拿一次页编程/擦除超时当整个请求最坏耗时。

## 7. 双卷头与显式格式化

分区前两个物理块 A/B 专用于卷描述，不参与逻辑组分配。描述含格式版本、epoch、几何/容量、PREPARING/READY 与 CRC；阶段记录在不同页追加，不原地任意改状态值。普通文件写入不更新卷头。

重格式化：

1. 保留有效 A，擦除 B 并建立、验证新 epoch PREPARING。
2. 在 A 建立并验证相同 epoch PREPARING。
3. 两份 PREPARING 都验证后才允许破坏性擦除数据区。
4. 全部数据区擦除验证后，追加验证 B READY，再追加验证 A READY。
5. 两份 READY 都验证后才报告格式化成功。

恢复选择可验证的最新 epoch。新 PREPARING 不能被旧 READY 掩盖；最新 epoch 无 READY 则报格式化未完成/未就绪，不自动回退、不自动续格式化。B READY 有效而 A 仍同 epoch PREPARING 表示数据区格式化已完成，可依 READY 恢复；调用者未收到成功不代表介质未变化。

FTL 格式化建立底层卷和空闲块；FatFs 格式化在逻辑扇区上建立 FAT。首次使用先前者后后者；普通挂载失败不自动触发任何格式化。

## 8. 扫描恢复、内存与日志

RAM 表不是持久化来源，也不是从 Flash 某个完整映射表直接拷贝。每次打开卷清理重建，NOLOAD/SDRAM 残留不可信。

先扫描每个数据物理块的头/提交页，依据提交页独立 CRC 筛选当前 epoch 记录并按版本选择；再完整读取获选 4 KiB，校验头、提交绑定和数据。旧失效块可能被 GC 擦坏头部而仍保留提交页，不能因此在选择最新版之前误报卷损坏。最新已提交数据损坏则报告损坏，不静默选旧记录；未提交记录不可见。空头/空提交页不能证明全块擦除，需全块验证或重新擦除后才可分配。

24 MiB 默认头/提交扫描约 3 MiB；实现为每个获选版本读取完整 4 KiB，全部 5527 组有数据时约 21.6 MiB，合计约 24.6 MiB，另加卷头和全空候选块验证。此为字节数估算，不是实测耗时；只保留表和复用工作区，不把整片内容拷到 SDRAM。

| 内存 | 默认估算 | 位置与所有权 |
| --- | ---: | --- |
| 每组 32 位物理索引（按物理块容量预分配） | 约 24 KiB | SDRAM，Platform 持有注入 |
| 每组 64 位版本（按物理块容量预分配） | 约 48 KiB | SDRAM，Platform 持有注入 |
| 每数据物理块 8 位状态 | 6 KiB | SDRAM，Platform 持有注入 |
| 组工作区 / 校验区 | 4096 B / 512 B | MDMA 可达内部 SRAM，32 B 对齐，Platform 持有注入 |

按实际逻辑组紧凑分配理论约 71 KiB；当前 Platform 为避免依赖 FTL 私有预留比例，按 6142 个数据物理块预分配 Map/Versions，表链接段 `.flash_ftl_tables` 实占 79,872 B（78 KiB，SDRAM NOLOAD），未计 Handle、统计和工作区。同时最多一个 FTL 操作，复用工作区。Cache 维护沿 Adapter 接缝完成；检查整条 Cache line 所有权及硬件停止访问后才能复用。

FTL 提供诊断快照，经 Platform 转交，Service 按宏格式化并 `Service_Log_Post()`。启动摘要包含格式版本、epoch、有效组、已擦除空闲块、失效块、扫描耗时，不逐扇区输出。默认关闭不屏蔽错误，也不改变扫描/GC/提交。统计示例不得当成实测。

## 9. GC、分配与磨损

分配游标循环找已擦除空闲块，GC 游标循环找可回收块。旧组版本整块失效，普通 GC 直接擦除，无需搬迁混合有效扇区。未提交/中断擦除块须经恢复分类确认不承载当前有效数据后才可回收。

预留 R 是全池预算，不是固定地址区域；空闲 F 只计确认擦除块。F 不高于 `ceil(R * 75 / 100)` 时启动，达到 `ceil(R * 90 / 100)` 时停止，默认 462 / 554 块。区间保持滞回。无可回收块时结束回收，不忙循环、不擦有效数据。

前台分配触及保护下限先 GC，不能安全分配则报空间不足；比较为 F <= 2 时先回收，F > 2 时才允许分配并扣减，不能让分配越过保护下限。

```text
StorageTask 空闲回收机会 -> Service -> Platform -> FTL 判断/选块
  -> RawOps 擦除 -> IRQ 通知 -> 普通上下文收尾
  -> 擦除校验 -> 更新空闲状态
```

回收采用有限工作推进，不一次无限清空 GC 队列。主循环不能无限期只等 SD 检测通知，需安排回收机会；在飞操作按 IRQ/超时推进。不承诺 NOR 擦除可即时抢占；周期/批量预算实现时配置验证。

首版不做静态磨损均衡、冷数据搬迁或持久化擦除计数。游标掉电丢失不影响映射恢复，频繁重启可能造成磨损偏向。统计只代表本次上电，不当作寿命累计或全盘均匀磨损保证。

## 10. 推进、错误、安全收尾与恢复

Process 区分可继续软件步骤、等待硬件、成功结束、失败/收尾中；不能 BUSY 就一律睡眠，软件步骤未必产生 IRQ。Service 只在需要硬件时等待通知并设置超时唤醒。

Platform 按操作类型派发：原始诊断直接推进 W25Qxx，FTL 操作只推进 FTL，由其经 Bridge 推进 W25Qxx，不能重复推进底层同一操作。一次原始页完成不等于 FTL 请求完成。

| 情况 | 处理 |
| --- | --- |
| 空指针、非法长度、LBA 越界 | 拒绝参数，不破坏正常状态 |
| 操作在飞 | 返回忙，不并发复用实例/缓冲 |
| 空间不足 | 必要 GC 后仍不足则失败，不覆盖有效块 |
| 读/编程/擦除超时或介质错误 | 停止请求，安全收尾，保持故障并拒绝新请求 |
| 提交前校验失败 | 保留旧映射，停止，不自动换块重试 |
| 最新已提交记录 CRC 失败 | 报损坏，不静默回滚 |
| 格式不兼容/格式化未完成 | 未就绪并可诊断，不自动格式化 |

软件超时不代表控制器/MDMA 停止，更不代表 NOR 内部擦写停止。阻止新请求后，确保硬件不再访问缓冲区才能复用，无法确认则保持隔离。RawOps 必须表达失败安全收尾，不能只有 Start/Process，也不能承诺撤销已发送的 NOR 操作。

显式恢复先确认硬件安全，再重新打开扫描 FTL，随后 Service 处理卷生命周期；故障前 FatFs 文件对象不能透明继续写。提交完成但通知丢失可能返回失败，因此失败不代表未写入。

Platform 在整个 FTL 请求期间关闭映射，不在每个内部页操作后短暂恢复；成功且硬件空闲后按入口状态恢复，失败保持关闭。资源消费者不得同期访问映射地址。字体/模型预加载 SDRAM 属上层可选方案，本轮不实现。

## 11. 接口能力与启动

除 BSP 名称已确定外，下表是能力清单，不是已存在函数声明；签名及稳定状态枚举实现前落定。

| 层 | 能力 |
| --- | --- |
| Service | 执行器初始化、同步读写/同步、回收、显式恢复、挂载/显式格式化 |
| Platform | 绑定、打开卷、逻辑读写/同步/回收/格式化启动、推进、状态/几何/诊断、安全恢复 |
| FTL | 绑定检查、启动操作、推进、结果/几何/统计；不接收 FatFs 类型 |
| RawOps | 几何、原始读/编程/擦除及进度/结果、安全收尾；Context 覆盖所有在飞操作 |

启动：SDRAM/QSPI/W25Qxx 就绪（破坏性 SDRAM 自检早于内存使用）→ Platform 绑定 FTL/分区/内存 → StorageTask 初始化 Service 执行器并长期订阅 → 打开卷并扫描验证 → 通过 USERPath 挂载 → 文件流程与回收。USER 初始化遇已就绪直接返回，不重复扫描。

原始诊断只访问既有受限区域，不借自检 API 操作 FTL。SD/Flash 独立初始化、挂载和错误状态，一个未就绪不能让另一个伪报成功或失败。

## 12. 已实施的文件范围

| 位置 | 实施内容 |
| --- | --- |
| `Components/flash_ftl/` | 新增公开头、实现、私有配置：编解码、状态机、映射、GC、恢复、诊断 |
| `Adapters/bridge/flash_ftl_w25qxx/` | 新增 Bridge 头/实现，绑定、地址边界和状态转换 |
| `Components/w25qxx/`、`Adapters/stm32_hal/w25qxx_qspi/` | 核查补齐安全收尾，尤其读超时 DMA 与 NOR 仍忙，不借机增加 TX MDMA |
| `Platform/flash/platform_flash.c/.h`、`platform_flash_config.h` | FTL 装配、内存、分区、操作派发、映射生命周期、诊断 |
| `FATFS/Target/bsp_driver_user_diskio.c/.h` | 新增自维护契约与安全弱定义，不宣称由 CubeMX 生成 |
| `FATFS/Target/user_diskio.c` | 仅 USER CODE 区包含契约并薄转发 |
| `Service/filesystem/sd/` | 原 `filesystem_fatfs_bsp.c` 改名 `filesystem_sd_bsp.c` 并迁入；迁入 SD 私有执行器及头 |
| `Service/filesystem/flash/` | 新增 `filesystem_flash_bsp.c`、`filesystem_flash_transfer.c/.h` 与私有配置 |
| `Service/filesystem/filesystem_service.c/.h` 及配置 | 保留单个 Module；公开头按卷/文件/诊断切开，增加 Flash 卷、回收/恢复能力 |
| `APP/tasks/storage/flash/storage_flash.c`、相关 benchmark | 执行职责交 Service；APP 保留启动/诊断编排及回收时机，不留第二回调所有者 |
| 自维护 CMake 与必要链接脚本 | 核查源收集、强符号、SDRAM/DMA SRAM 容量对齐，不向生成构建嵌入产品逻辑 |
| `Tests/flash_ftl/`与相关测试 | 纯 C Fake NOR、故障注入，与固件构建隔离 |

上述代码已按用户后续授权实施。软件测试与硬件验收分开记录，不把编译通过等同于真实掉电验证。

## 13. 验收要求

最低验收：

- 单扇区/整组/跨组/首尾 LBA、溢出、未映射 `0xFF`、部分更新保留其他内容。
- 全 `0xFF` 页跳过编程但校验；单页单次编程；新写失败保留旧映射。
- 头、每个数据页、提交页及回读前后掉电；已确认持久、未确认可旧/新、跨组不误承诺原子性。
- A/B PREPARING、擦除、B/A READY 各点掉电；最新 epoch 选择，不自动格式化。
- 不完整头/提交、已提交数据损坏、版本歧义、几何不兼容、头空而块不空的恢复分类。
- GC 水位/保护下限、无失效块、擦除中掉电、持续覆盖写；不越界、不擦当前有效块。
- 中断丢失、超时、延迟旧通知、DMA 中止失败、NOR 仍忙；缓冲不提前复用、映射不提前恢复。
- USER LUN/卷路径、弱后端安全失败、强定义接管、CubeMX 接缝保留。
- FatFs 写入/同步/重挂载内容校验，独立 SD 回归；实测容量、内存、扫描/读写耗时。

Fake NOR 约束擦除值 `0xFF`、仅 1→0 编程、页边界和撕裂写/擦除；重启清空 RAM。真实断电不可用复位测试完全替代。CRC 并非无碰撞的原子提交原语，保证依赖明确故障模型，不承诺任意欠压、电气损坏或 CRC 碰撞可恢复；单组 FTL 原子性不自动保证 FatFs 元数据事务一致性。

已落定格式、接口和预算见下文。以后若需改变已通过的容量、完成或恢复语义，先反馈再调整。

## 14. 格式版本 1 的实际编码

所有整数按小端逐字段编码，不把 C struct 直接落盘。所有元数据页为 256 B，未使用字节填 0xFF；CRC 位于 252..255，覆盖 0..251。CRC32 为反射算法，多项式 0xEDB88320，初始值 0xFFFFFFFF，输出逐位取反；不使用硬件 CRC 外设。数据 CRC 分别覆盖对应完整 512 B 扇区。

| 记录 | 字段偏移（字节） |
| --- | --- |
| 卷 PREPARING / READY 页 | magic@0，format@4，epoch:u64@8，phase@16，数据物理块数@20，逻辑组数@24，扇区字节数@28，块字节数@32，组扇区数@36，CRC@252 |
| 组头页 | magic@0，format@4，epoch:u64@8，逻辑组号@16，组版本:u64@20，七个扇区 CRC@28..55，页 CRC@252 |
| 提交页 | magic@0，format@4，epoch:u64@8，逻辑组号@16，组版本:u64@20，头页 CRC@28，提交页 CRC@252 |

除 u64 外的已列字段均为 u32。卷 magic=0x314C4F56，头 magic=0x31524746，提交 magic=0x31544D43；format=1，PREPARING=1，READY=2。两卷块各用页 0 存 PREPARING、页 1 存 READY。数据块组头位于 0，七扇区数据位于 256..3839，提交页位于 3840..4095。格式化卷头擦除后也要全块验证，不能在旧 READY 页残留时继续。

epoch 首次为 1，每次显式格式化递增；组版本从 1 起递增，0 无效，达到 UINT64_MAX 拒绝继续并返回 INCOMPATIBLE。同组同版本的两个有效提交报 CORRUPT。有效提交指明的获选头/数据损坏报 CORRUPT；提交 CRC 无效按未提交处理，无法区分撕裂提交与事后提交页损坏，这属于 CRC 故障模型限制。没有任何有效卷描述返回 UNFORMATTED（即使存在无法解析的残片），但永不因此自动格式化。最高 epoch 只有 PREPARING 返回 INCOMPLETE，几何/版本不匹配返回 INCOMPATIBLE。

## 15. 对外流程和实际预算

Service 公开 InitFlash、MountFlash、UnmountFlash、FormatAndMountFlash、RecoverAndMountFlash、ReclaimFlash。首次使用时由 Storage Task 初始化执行器，启动诊断完成后挂载已有卷。FormatAndMountFlash 是显式破坏性 API：先注销旧 FAT 卷，再格式化 FTL，最后用 FM_FAT | FM_SFD 建 FAT12/16 并重新挂载；不会在启动路径执行。RecoverAndMountFlash 注销旧文件对象、确认硬件空闲后重扫 FTL，成功返回时卷已经挂载。

USER 驱动内 LUN 为 0，与全局 1:/ 分开；GET_SECTOR_COUNT 返回 38689（默认），GET_SECTOR_SIZE 返回 512。GET_BLOCK_SIZE 返回 1：本版 FatFs 要求其为二次幂，七扇区组是 FTL 内部约束。CTRL_SYNC 经过同步执行器，未知命令返回 RES_PARERR；未就绪返回 RES_NOTRDY，传输失败返回 RES_ERROR。

| 所属配置文件 | 默认值与含义 |
| --- | --- |
| Platform/flash/platform_flash_config.h | 物理 24 MiB，从尾部自检块之前向前保留；修改容量必须重新核验已有数据、资源/镜像边界和格式兼容性 |
| Components/flash_ftl/flash_ftl_config.h | 预留 10%；GC 启动/停止为预留预算的 75%/90%；空闲保护下限 2 块 |
| Service/filesystem/flash/filesystem_flash_config.h | 摘要日志开关 0；通知重查 2 ms；读 30 s、写/打开 120 s、全格式化 3600 s、恢复/单次回收 2 s；每 64 个软件步骤让出一次调度 |
| APP/tasks/storage/storage_task_config.h | 空闲回收机会默认 100 ms；每次最多擦一块，不保证擦除可抢占 |

这些预算是保守的软件超时，不是硬件实测速率或最长响应时间保证。任务同步调用期间可阻塞让出 CPU；返回超时前仍须确认控制器/DMA 安全停止。若硬件始终不能 Quiesce，执行器保持等待而非让 DMA 访问已归还的缓冲；该极端情况需板级看门狗/复位策略处理。NOR 内部擦写不会被控制器 Abort 取消，恢复必须另查 WIP=0 与 QE。

## 16. 本轮验证记录（2026-08-31）

- 主机 `Tests/flash_ftl`：FTL 行为、真实 FatFs/USER Glue 文件写入同步及清 RAM 重挂载、无后端安全失败，三项 CTest 通过。
- Fake NOR 检查页边界、仅 1→0 编程、撕裂写/擦除。覆盖完整/部分/跨组和尾 LBA、溢出/忙、未映射值、全 FF 跳页、持续覆盖写及前台 GC、后台单块预算与停止水位。
- 掉电注入覆盖全部 16 个编程页各五种撕裂长度、格式化各写擦阶段、旧版本 GC 擦除撕裂、最新已提交 payload 损坏及安全收尾等待；卷头虚假擦除成功的测试先失败，补齐全块验证后通过。
- `Tests/w25qxx` 既有回归及新增 Quiesce/恢复 WIP、真实 Bridge 分区越界拒绝/地址转换/异步推进场景通过。FatFs 主机测试替换 BSP 硬件后端，不覆盖 Service/FreeRTOS/QSPI 时序；MinGW 的默认实现测试去除 weak 属性仅验证行为，强弱符号覆盖另由 ARM 链接检查。
- Debug 与 Release 固件均成功构建；五个 BSP_USER_DISKIO_* 是来自 Service 的强符号。SD 文件移动保持执行逻辑。未执行 CubeMX 重生成。
- Debug 第一内部 Flash 区使用 502,988 / 524,288 B（95.94%），Release 为 343,476 B（65.51%）（含内部枚举类型调整后的最新构建）；存在既有第三方/GUI 警告及 RWX 链接段警告，本轮未修改生成 GUI。

仍需上板：SD 热插拔/读写回归、真实 QSPI/MDMA 中断丢失/延迟/中止失败、欠压掉电、分区保护、格式化与重挂载、启动扫描/读写/GC 延迟、Cache 可见性及长时间磨损行为。本轮未烧录、未格式化任何实际设备，软件回归不替代这些验收。


## 17. 函数注释核查（2026-08-31）

本次按 [代码注释规范](coding_standard.md#4-doxygen-与行内注释) 对 FTL 链路及相关测试逐函数核查。规范已明确：所有自维护函数实现均须写 Doxygen，包括私有函数、回调、强/弱后端、测试入口与辅助函数；所有仅声明的位置不写逐函数 Doxygen。头文件保留类型、字段、宏及回调类型契约说明。

核查范围和覆盖：

| 自维护实现范围 | 函数定义数 |
| --- | ---: |
| Flash FTL Component | 37 |
| FTL/W25Qxx Bridge | 9 |
| Flash Service BSP 与同步执行器 | 23 |
| USER DiskIO 安全弱后端 | 5 |
| Platform Flash | 39 |
| Filesystem Service 根实现 | 13 |
| Storage Flash 与 Storage Task | 6 |
| W25Qxx Component 与 HAL QSPI Adapter | 42 |
| 迁移后的 SD BSP 与同步执行器 | 17 |
| FTL、弱后端及 W25Qxx 主机测试 | 69 |
| 合计 | 260 |

本次在 9 个 C 文件中新增 91 处函数 Doxygen，补齐 5 个 SD BSP 函数的形参说明，修正 Platform 同步诊断读取的形参名称，并将 HAL BusOps 表的错位注释移回对象定义。说明覆盖参数、返回语义、同步等待、单一执行上下文、缓冲生命周期、安全收尾以及测试替身边界。已有完整注释未重复插入。

相关 C/H 的源码核查确认：上述 16 个自维护 C 文件的 260 个函数定义均有对应 Doxygen，形参名称逐项匹配，非 void 函数有返回说明；函数声明没有逐函数 Doxygen。CubeMX 生成的 user_diskio.c 模板说明及 USER CODE 薄转发保持原样，不将生成函数计入自维护函数覆盖率；未修改第三方或 GUI 生成文件。本次覆盖范围不等同于全工程历史代码注释已经整改完毕。

验证结果：

- 所有检查范围内 C/H 文件与修改前相比，去除注释和空白后的代码符号序列完全一致，未改变接口、枚举、控制流或 Flash 格式。
- 四项已有 CTest 通过，Debug/Release 固件均构建成功；两种固件的 BIN 与本轮注释修改前逐字节相同。
- 环境未安装 Doxygen，本次验证的是源码注释关联、标签和参数一致性，未生成或检查 Doxygen HTML。
- 保留既有 RWX 链接段警告；未烧录或对实际设备执行格式化，硬件验收边界不变。


## 18. Flash 文件 benchmark 与删除边界

本轮新增 Service 文件封装及挂载后的 APP 文件基准。链路为 APP benchmark →
Service 文件接口 → FatFs → USER DiskIO → Service 块后端 → Platform → FTL → RawOps。
实现、默认 1 MiB/4 KiB 参数、计时范围和失败清理详见
[benchmark 说明](../APP/tasks/storage/benchmark/README.md)；
静态文件/目录槽、句柄代次和 UTF-8 相对路径范围见 [Service 说明](../Service/filesystem/README.md)。

仅新建本轮测试文件，同名拒绝覆盖或删除；写入同步后重新打开读取、独立校验，最后关闭并删除。
异常也尝试清理，但介质/锁故障无法删除时必须明确报告残留。未挂载或无文件系统时跳过测试，
不自动调用两层格式化，也不在测试结束时强行擦物理地址。

f_unlink 使 FatFs 文件目录项与簇链释放并同步，不是安全擦除。当前 _USE_TRIM=0，
后端未实现 CTRL_TRIM；FTL 不知道哪些有效 LBA 已被文件系统释放。
直到这些 LBA 被重新写入且新版本提交，旧物理版本才失效并可由 GC 回收。
目录/FAT 自身的更新仍会经过 FTL 异地写与 GC，但不等于文件 payload 已直接失效。

本轮验证：新增 12 个文件场景与原有 4 项回归共 16 项 CTest 通过；
正常删除后重挂载确认文件不存在且 FAT 空闲簇恢复。新增实现及测试通过
-Wall -Wextra -Werror 语法检查，相关 70 个函数定义的 Doxygen/形参/返回说明核查通过。
默认关闭基准的 Debug/Release 构建通过，第一 FLASH 区分别为 503116 B / 343556 B。
正式布局开启基准时，Debug 为 697492 B、Release 为 537044 B，均超过 524288 B；
因此不能宣称开启基准的正式固件构建通过。

仅在 build/flash-file-benchmark-Release 中验证了链接草案：将 cc936.c.obj 的
.rodata.oem2uni（87172 B）放到 FLASH2，Release 链接后第一 FLASH 为 449868 B，
FLASH2 为 463804 B，两者均小于各自 512 KiB。正式 stm32h743zgtx_flash.ld 未在本轮改动；
该布局调整及 Debug 的额外容量安排待用户确认。未修改 CubeMX 配置或生成文件，
未烧录、未对实际设备格式化；保留既有 RWX 链接警告，未生成 Doxygen HTML。

