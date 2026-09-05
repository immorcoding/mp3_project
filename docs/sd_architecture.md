# SD 卡子系统架构

> 适用工程：`version0.3.1` 及后续版本
>
> 当前后端：STM32H743 SDMMC1，4 位总线
>
> 当前调度：FreeRTOS Storage Task 通过索引 0 的直接任务通知处理 SD 热插拔，并通过索引 1 等待 SDMMC DMA 完成
>
> 当前范围：逻辑块访问、热插拔、同步 DMA FatFs DiskIO Bridge，以及由 Storage Task
> 串行执行的 FAT32 挂载与卸载。设备不格式化 SD。
>
> 相关 ADR：[ADR-0005：SD、FatFs 与 Storage Task 的所有权](adr/0005-sd-filesystem-task-ownership.md)、[ADR-0006：Cortex-M7 D-Cache 范围与 DMA 缓冲所有权](adr/0006-cache-range-ownership.md)

## 1. 设计目的

本文记录当前已经接入 FatFs 的可移除 SD 卡栈。当前实现包含：

- 一个可复用、面向逻辑块的 SD Card Device；
- 一个绑定 STM32H743 SDMMC1 的具体 Port；
- 一个代表本板唯一物理卡槽的 Platform 接口；
- `Adapters/stm32_hal/irq` 下两个源特定的 STM32 IRQ Adapter：GPIO EXTI 的 PinMask 回调分发，以及 SDMMC 的传输完成/错误分发；
- 一个独占 SD 初始化、消抖、FatFs 卷生命周期和介质事件日志的 Storage Task；
- 一个封装 FatFs 逻辑卷操作、同步 DMA 执行器和 Cube BSP Override Adapter 的 Filesystem Module。

当前不抽象通用 `BlockDevice`。SD 与 Flash FTL 已是两个真实块后端，仍各走独立 DiskIO；不从二者再抽公共块设备接口。

除第 2 节的目录/所有权说明外，本文的调用图均为**运行时请求或事件路径**，不表示 C 头文件依赖。功能/抽象所有权、编译期依赖与装配规则以 [architecture_standard.md](architecture_standard.md) 为准。

## 2. 目录与职责

```text
APP/
  app.c                         顶层启动编排和调度器启动前日志

Components/sd/
  sd.h                          可复用 Device 公共类型和接口
  sd.c                          状态机、参数校验、块访问和错误处理

Adapters/cortex/cache/
  cortex_m7_dcache_adapter.h/.c Cortex-M7 Cacheable 内存范围的 D-Cache 维护

Adapters/stm32_hal/sd/
  sd_stm32_hal_adapter.h        STM32 HAL SDMMC Adapter 绑定接口
  sd_stm32_hal_adapter.c        STM32 HAL SD/GPIO、DMA 启动和 Cache Adapter 调用

Adapters/stm32_hal/irq/
  stm32_gpio_exti_irq.h/.c     GPIO EXTI 回调节点、注册接口和 PinMask 链表分发
  stm32_sdmmc_irq.h/.c         SDMMC Handle 回调节点、HAL 回调注册和传输事件分发

Platform/sd/
  platform_sd.h                 Storage 使用的 Platform 接口、检测与传输通知回调类型
  platform_sd.c                 私有 Device、GPIO EXTI/SDMMC IRQ 节点、hsd1/SD_CD 装配

APP/tasks/storage/
  storage_task.c                FreeRTOS 通知、30 ms 消抖和调度循环
  sd/storage_sd.h/.c            Platform SD 生命周期、FatFs 卷管理和介质日志
  flash/storage_flash.h/.c      Flash 执行器启动、挂载策略与空闲回收

Service/filesystem/
  filesystem_types.h            逻辑卷与路径/名称上限
  filesystem_service.h/.c       FatFs 卷生命周期 Service Interface
  filesystem_file.h             卷感知文件 Interface
  filesystem_directory.h        卷感知目录 Interface
  filesystem_flash_access.h     启动诊断 Interface
  filesystem_status.h/.c        私有 FRESULT 映射与盘符路径
  sd/filesystem_sd_transfer.h/.c   Storage Task 独占的同步 DMA 执行器与中转缓冲区
  sd/filesystem_sd_bsp.c           BSP_SD_* 强定义，连接 Cube DiskIO Override Seam

FATFS/App/
  fatfs.c                       CubeMX 逻辑卷对象与 Driver Link

FATFS/Target/
  sd_diskio.c                   Cube DiskIO Glue；经 BSP_SD_* Override Seam 到达 Filesystem

Core/Inc/sdmmc.h
Core/Src/sdmmc.c                CubeMX 管理的 hsd1 和 HAL MSP 初始化/反初始化
```

`sd.c` 不得包含 `sdmmc.h`、`main.h` 或 STM32 HAL 头文件。具体 Adapter
可以认识 STM32 HAL 类型，但不固定使用 `hsd1` 或某个检测引脚。Platform 通过
`SDCard_STM32HALAdapterTypeDef` 装配 `hsd1`、SD_CD GPIO 和低有效电平，
并私有持有 Device 句柄，上层不能直接修改其状态。

## 3. 启动运行时路径

### 3.1 插卡启动

```text
app_init()
  -> Platform_Init()                          初始化整机强依赖设备
  -> app_task_start()
     -> Create Task
        -> Service_Log_Init()
        -> 创建 Log Task、Storage Task 与 Monitor Task

Storage Task
  -> storage_task()
      -> xTaskGetCurrentTaskHandle()
      -> storage_sd_init(task_handle)
         -> Platform_SD_Init(storage_sd_detect_callback, task_handle)
            -> SDCard_STM32HALAdapter_Bind(&Device, &Adapter)
            -> GPIOEXTI_STM32HALAdapter_Register(
                   &DetectIRQCallback,
                   SD_CD_Pin,
                   platform_sd_detect_irq_cb,
                   &hplatform_sd)
            -> SDCard_Init(&Device)
               -> Port.IsPresent(PC7)
               -> Port.Init(&hsd1)
                  -> HAL_SD_Init(&hsd1)
                     -> CubeMX 生成的 HAL_SD_MspInit()
               -> Port.GetInfo(&hsd1)
                  -> 缓存归一化逻辑块信息
               -> State = READY
          -> Service_Filesystem_InitSD(STORAGE_NOTIFY_SD_TRANSFER)
             -> filesystem_sd_transfer_init(notify_index)
            -> Platform_SD_SetTransferCallback(
                   filesystem_sd_transfer_irq_callback, ...)
            -> Platform 私有地 STM32SDMMCIRQ_Register(
                   &TransferIRQCallback, &hsd1, ...)
```

初始化成功后，Storage Task 通过 `Platform_SD_GetInfo()` 取得副本并记录容量日志，不直接接触私有 Device 句柄或 `hsd1`。

### 3.2 未插卡启动

```text
Platform_SD_Init()
  -> SDCard_Init()
     -> IsPresent() == false
     -> 清除缓存信息有效标志
     -> State = NOT_PRESENT
     -> return SDCARD_OK
  -> return PLATFORM_OK
```

没有插卡是正常的持续状态，不是整机故障。因此系统可以继续启动。之后若在 `NOT_PRESENT` 状态读取逻辑块或获取卡信息，因为该次操作无法完成，函数仍会返回失败。

## 4. 块传输路径

`SDCard_ReadBlocks()` / `SDCard_WriteBlocks()` 仍保留为阻塞轮询路径，用于启动诊断或不需要 DMA 的调用者。它们校验块范围后进入 `BUSY`，调用 Port 的阻塞传输和 `Sync()`，最后回到 `READY`。

FatFs 当前使用同步 DMA Bridge：对 DiskIO 来说调用仍同步返回，但 Storage Task 在等待期间让出 CPU。

```text
FatFs disk_read / disk_write
  -> Service/filesystem/sd/filesystem_sd_bsp.c 的 BSP_SD_ReadBlocks / BSP_SD_WriteBlocks
  -> Service/filesystem/sd/filesystem_sd_transfer.c
  -> 复制到 32 KiB、32 字节对齐的 AXI SRAM 中转缓冲区
  -> STM32 HAL SD Adapter 按方向委托 Cortex-M7 Cache Adapter 执行 Clean 或 Clean+Invalidate
  -> Platform_SD_StartReadBlocks / Platform_SD_StartWriteBlocks
  -> SDCard_Start*()：State = BUSY，只启动 HAL_SD_*Blocks_DMA()
  -> Storage Task 在 STORAGE_NOTIFY_SD_TRANSFER 阻塞等待 SDMMC IRQ
  -> Platform_SD_CompleteTransfer()：检查卡状态并使 Device 回到 READY 或 ERROR
  -> 读取方向由 SD Adapter 委托 Cache Adapter Invalidate 缓冲区，再 memcpy 到 FatFs 原始缓冲区
```

写入的数据阶段完成后，SD 卡内部仍可能处于编程状态，因此 `CompleteTransfer()` 的同步尤其重要：只有卡回到 `TRANSFER` 才能安全接受下一条命令。

中转而不是直接 DMA 到 FatFs 提供的指针有两个目的：FatFs 的指针不保证 32 字节对齐，也不保证它所在的 RAM 对 SDMMC DMA 可见；并且中转区把 Cache 操作精确限制在专用缓冲区，避免对相邻变量造成影响。

## 5. 热插拔事件路径

```text
EXTI9_5_IRQHandler()
  -> HAL_GPIO_EXTI_IRQHandler(SD_CD_Pin)
  -> HAL_GPIO_EXTI_Callback(SD_CD_Pin)
  -> GPIOEXTI_STM32HALAdapter 遍历回调链表
  -> PinMask 匹配 DetectIRQCallback
  -> platform_sd_detect_irq_cb(SD_CD_Pin, &hplatform_sd)
     -> storage_sd_detect_callback(StorageTaskHandle)
        -> xTaskNotifyIndexedFromISR(STORAGE_NOTIFY_EVENT, STORAGE_NOTIFY_FLAG_SD_DETECT, eSetBits)

Storage Task
  -> SD 状态：未就绪 / 消抖中 / 就绪；消抖与回收各记 start tick
  -> xTaskNotifyWaitIndexed(STORAGE_NOTIFY_EVENT) 超时取两路剩余较小值
  -> 若置 STORAGE_NOTIFY_FLAG_SD_DETECT：立刻进消抖中，重开 30 ms 起点（离开就绪）
  -> 消抖截止到点：Platform_SD_Process(&event)
        -> Platform_SD_Refresh()
        -> 仅在持续状态变化时产生 INSERTED 或 REMOVED
        -> 按 SD 是否已挂载回到就绪或未就绪
  -> 非消抖中：若有 PENDING 则 storage_listbuffer_load()
  -> 回收截止到点：storage_flash_reclaim()
```

### 5.1 为什么使用 FLAG 而不是计数

卡座触点抖动可能产生多个边沿，但边沿次数没有业务意义。系统只关心“检测输入发生过变化，请在稳定后重新读取”。Storage Task 将 `STORAGE_NOTIFY_FLAG_SD_DETECT` 视为重新开始消抖窗口的信号，不把通知值解释为插拔次数。Queue 窗口请求与卡检测共用 `STORAGE_NOTIFY_EVENT` 槽，靠 FLAG 区分，避免主循环轮流阻塞两个 indexed 槽。

### 5.2 为什么由 Platform SD 持有 GPIO EXTI Callback

GPIO EXTI Adapter 只认识 STM32 HAL 的 GPIO PinMask，不认识 SD、PMIC 或产品逻辑中断源。
`Platform_SD` 长期持有 `DetectIRQCallback`，初始化时注册、反初始化时注销，
回调上下文直接指回自己的私有 Handle。Platform 的原始 ISR 回调再调用
`Platform_SD_Init()` 注入的 `DetectCallback`；当前该回调由 Storage Task 的
`storage_sd_detect_callback()` 实现为
`xTaskNotifyIndexedFromISR(..., STORAGE_NOTIFY_EVENT, STORAGE_NOTIFY_FLAG_SD_DETECT, eSetBits)`，但 Platform 本身不包含 FreeRTOS。

因此不再需要额外的 `Platform_IRQ_SourceTypeDef`、GPIO 到逻辑源的映射表或
Platform IRQ 二次分发。以后新增按键或 PMIC 中断时，由各自模块持有并注册新的
Callback 对象，GPIO EXTI Adapter 的实现不需要修改。

### 5.3 为什么使用直接任务通知

`volatile` 只保证每次都真实访问内存，不保证“读取并清除”这个复合操作不可被 ISR 打断。直接任务通知由 FreeRTOS 原子维护，且 Storage Task 是唯一接收者；它既没有额外队列存储，也不需要跨文件全局初始化标志。

ISR 只调用 `xTaskNotifyIndexedFromISR(..., STORAGE_NOTIFY_EVENT, STORAGE_NOTIFY_FLAG_SD_DETECT, eSetBits)`，Storage Task 使用 `xTaskNotifyWaitIndexed()` 等待同一槽。检测 FLAG 把 SD 状态切到消抖中并重开 `start` tick；经过 30 ms（无符号 tick 差）且无新边沿后才 `Platform_SD_Process()`。`wait` 超时是消抖剩余与回收剩余的较小值，不用 `vTaskSetTimeOutState`。窗口 `request` 仅 SD 就绪时成功。

### 5.4 FreeRTOS 实现

当前实现中，`Platform_SD_Process()` 不再保存检测通知或主动等待；它只在消抖完成后调用 `Platform_SD_Refresh()` 并翻译 `INSERTED` / `REMOVED` 事件。Device 状态机、Port、错误模型和 Platform 公共接口都不依赖 FreeRTOS。

### 5.5 SDMMC DMA 完成路径

```text
SDMMC1_IRQHandler()
  -> HAL_SD_IRQHandler(&hsd1)
  -> HAL 按 hsd1 注册的 Rx / Tx / Error / Abort callback
  -> STM32SDMMCIRQAdapter（按 SD_HandleTypeDef 匹配）
  -> Platform SD 私有转发 callback
  -> filesystem_sd_transfer_irq_callback()
     -> xTaskNotifyIndexedFromISR(STORAGE_NOTIFY_SD_TRANSFER, event, eSetValueWithOverwrite)

Storage Task 上下文中的 Filesystem DMA executor
  -> xTaskNotifyWaitIndexed(STORAGE_NOTIFY_SD_TRANSFER)
  -> Platform_SD_CompleteTransfer(event)
```

`STORAGE_NOTIFY_SD_TRANSFER` 保存的是一次 DMA 的完整事件值，不是计数；它只能由正在执行 DiskIO 的 Storage Task 等待。卡检测边沿与窗口请求留在 `STORAGE_NOTIFY_EVENT` 的 FLAG 上，因此拔卡边沿不会被误当作 DMA 传输完成，也不会被 30 ms 消抖延迟为传输结果。槽位由 APP 枚举注入；`MountSD`/`UnmountSD` 不得把未绑定的执行器默认到事件槽。

## 6. 状态、返回值与诊断

这三类值用途不同：

| 值 | 含义 |
| --- | --- |
| `SDCard_StatusTypeDef` | 某一次 Device 函数调用是否成功。 |
| `SDCard_StateTypeDef` | 后续调用和调试器都能观察到的持续生命周期状态。 |
| `SDCard_ErrorTypeDef` | 最近一次 Device 失败发生在哪个语义阶段。 |

Device 句柄还保存：

| 字段 | 作用 |
| --- | --- |
| `LastPortStatus` | 与后端无关的底层结果分类。 |
| `Info` | 缓存的归一化容量和逻辑块信息。 |
| `IsInfoValid` | `Info` 当前是否可以复制给调用者。 |
| `IsPortInitialized` | 反初始化时是否需要释放 Port 资源。 |

`Platform_SD_GetDiagnostics()` 只复制 `DeviceError` 和 `PortStatus`，不会暴露私有 Device 句柄。若归一化信息不足，再用调试器查看具体 Port 中 `hsd1.ErrorCode` 的 HAL 原始错误位。

## 7. 状态机

```text
RESET
  |-- Init，无卡 ----------------------> NOT_PRESENT
  |-- Init，有卡且 HAL 成功 ----------> READY
  `-- Init 失败 -----------------------> ERROR

NOT_PRESENT
  `-- 稳定插入后 Refresh -------------> READY 或 ERROR

READY
  |-- 轮询 Read/Write/Sync ------------> BUSY --> READY
  |-- StartRead/StartWrite --DMA IRQ---> BUSY --> READY
  |                              |
  |                              `-- 超时/错误/中止 --> ERROR
  |-- Port 失败 -----------------------> ERROR
  `-- 稳定拔出后 Refresh -------------> NOT_PRESENT

ERROR
  |-- 显式重新 Init -------------------> READY / NOT_PRESENT / ERROR
  `-- 拔卡后 Refresh ------------------> NOT_PRESENT
```

`SDCard_Refresh()` 不负责机械触点消抖，也不能从 EXTI ISR 调用。当前由 Storage Task 在普通上下文以可重新开始的 30 ms 任务通知等待完成消抖，再调用 `Platform_SD_Process()` 和 `Refresh()`。

## 8. Device 为什么不是简单 HAL 包装

SD Card Device 集中隐藏了多处上层都需要的规则：

- 合法的生命周期顺序；
- 无卡状态处理；
- 缓存信息的有效性；
- 防整数溢出的块范围检查；
- `BUSY` 状态转换；
- 传输后的同步等待；
- 统一的 Device 和 Port 诊断；
- 部分初始化失败后的清理。

如果 `SDCard_ReadBlocks()` 只转调 `HAL_SD_ReadBlocks()`，APP 和 FatFs DiskIO 都要重复这些规则，Device 就失去了存在价值。

## 9. CubeMX 边界

CubeMX 继续管理：

- `hsd1`；
- `HAL_SD_MspInit()`；
- `HAL_SD_MspDeInit()`；
- SDMMC1 的时钟和 GPIO 复用配置；
- EXTI 和 SDMMC1 IRQHandler 的生成代码；
- SDMMC1 NVIC 使能与优先级。

当前不自动调用 `MX_SDMMC1_SD_Init()`。SD Card Port 直接调用有返回值的 `HAL_SD_Init()`，因为 CubeMX 生成的 `MX_SDMMC1_SD_Init()` 返回 `void`，失败时会进入 `Error_Handler()`，不适合可移除介质。

因此 `sd_stm32_hal_init()` 中保存了与 CubeMX 一致的以下参数：

- 时钟采样沿；
- 空闲时钟节能设置；
- 4 位总线宽度；
- 硬件流控；
- 时钟分频。

以后在 CubeMX 修改 SDMMC 配置时，必须同步核对该函数、`SDMMC1_IRQn` 和 HAL 回调注册开关。当前 Adapter 使用 `USE_HAL_SD_REGISTER_CALLBACKS = 1` 按 `hsd1` 注册回调，而不重定义全局 `HAL_SD_*Callback`，以免与 CubeMX BSP 模板冲突。自维护代码不应修改 CubeMX/ST 管理区；必须接入回调时，仅使用允许修改的 `USER CODE` 区。

## 10. FatFs 适配边界

当前 `Service/filesystem/sd/filesystem_sd_bsp.c` 强定义 CubeMX `BSP_SD_*`，使生成的
`sd_diskio.c` 通过 Filesystem Module 和 Platform SD 访问介质，而不是直接使用 `hsd1`。`fatfs.c`
仅保留逻辑卷对象和 Driver Link。映射关系为：

`disk_* → BSP_SD_*` 是 FatFs 经外部契约进入 Service-owned FatFs DiskIO bridge 的运行时入站 Seam；它不表示 `Service/filesystem` 反向包含 `FATFS/Target/sd_diskio.c`，也不改变本工程的功能/抽象所有权。

| FatFs DiskIO 操作 | Platform SD 接口 |
| --- | --- |
| `disk_initialize` | 检查已由 Storage Task 初始化完成的 `Platform_SD_GetState()` |
| `disk_status` | `Platform_SD_GetState()` 和 `Platform_SD_IsPresent()` |
| `disk_read` | 使用 `Platform_SD_StartReadBlocks()` 启动 DMA，等待索引 1 完成事件后调用 `Platform_SD_CompleteTransfer()` |
| `disk_write` | 使用 `Platform_SD_StartWriteBlocks()` 启动 DMA，等待索引 1 完成事件后调用 `Platform_SD_CompleteTransfer()` |
| `CTRL_SYNC` | `Platform_SD_Sync()` |
| `GET_SECTOR_COUNT` | `Platform_SD_GetInfo()` 返回的 `BlockCount` |
| `GET_SECTOR_SIZE` | `Platform_SD_GetInfo()` 返回的 `BlockSize` |

文件系统挂载状态不属于 `SDCard_StateTypeDef`。`Filesystem` Module 不会调用
`Platform_SD_Init()`；Storage Task 必须先完成卡检测、消抖和 Platform 生命周期，
再调用挂载；拔卡后由同一任务注销 FatFs 卷对象。当前 Storage Task 是 SD 与本地
FatFs 的唯一普通任务上下文。未来若出现 `Service/storage`，它只应接收跨任务命令并
协调文件打开状态，而不应复制 Storage Task 的 Platform SD 生命周期实现。

## 11. 当前限制

1. Filesystem DMA executor 只允许 Storage Task 作为唯一调用者；尚未实现 Mutex 或多任务文件系统服务；
2. 当前使用固定的 32 KiB、32 字节对齐 AXI SRAM 中转缓冲区。它每次传输最多 64 个逻辑块，大请求会串行分块；
3. Cache 操作仅覆盖该专用缓冲区。未来若直接 DMA 到其他缓冲区，必须重新满足 AXI SRAM 可访问性、32 字节对齐和 Clean/Invalidate 规则；
4. DMA 等待超时、错误或中止会使 Device 进入 `ERROR`；当前恢复策略是由后续卡检测/重新初始化恢复，尚未实现传输中的 HAL Abort 与原地重试；
5. 当前热插拔路径依赖 FreeRTOS Storage Task；若要回到裸机，应在应用层提供轮询通知和消抖策略，而不是把 FreeRTOS 依赖加入 Platform；
6. SD Card Port 的 SDMMC 初始化值、SDMMC1 NVIC 和 HAL 回调注册开关必须与 CubeMX 配置保持一致；
7. 已实现本地 FatFs 挂载与卸载；设备不格式化 SD。当前产品不支持 USB MSC，批量文件导入使用读卡器。

## 12. 对外使用规则

应用代码通常只调用 `Platform_SD_*`。`SDCard_*` 接口用于 Platform 装配、Device 独立测试和未来替换 Port；`hsd1` 始终是 SD Card Port 的实现细节。

架构变化时，应同时更新本文和根目录的 `CONTEXT.md`，重点核对：

- 实际目录；
- 启动与热插拔运行时路径；
- 状态机；
- 错误和诊断边界；
- FreeRTOS 替换接缝；
- FatFs 映射；
- 当前限制。

## Flash 接入后的目录划分（已实施）

[ADR-0010](adr/0010-fatfs-user-diskio-service-ownership.md) 已实施：Filesystem 私有实现划分为 `sd/` 和 `flash/`，SD BSP 为 `sd/filesystem_sd_bsp.c`，SD 私有执行器同步迁入。SD 契约、任务索引 1 及逻辑块语义保持；重新构建通过，硬件 SD 回归待执行。

SDCard Component 并未被省略，SD 卡内部控制器承担其 NAND 映射；外部原始 NOR 则需要独立 FTL。新 Flash 后端的索引 2 等待同样由 Filesystem Service 私有执行器持有，详见 [FTL 设计](flash_ftl_design.md)。
