# SD 卡子系统架构

> 适用工程：`version0.2.3`
>
> 当前后端：STM32H743 SDMMC1，4 位总线
>
> 当前调度：FreeRTOS Storage Task 通过直接任务通知处理 SD 热插拔
>
> 当前范围：逻辑块访问、热插拔、同步 FatFs DiskIO Bridge，以及由 Storage Task
> 串行执行的 FAT32 挂载、卸载与显式格式化。

## 1. 设计目的

本文记录当前已经接入 FatFs 的可移除 SD 卡栈。当前实现包含：

- 一个可复用、面向逻辑块的 SD Card Device；
- 一个绑定 STM32H743 SDMMC1 的具体 Port；
- 一个代表本板唯一物理卡槽的 Platform 接口；
- 一个按 GPIO PinMask 注册调用者回调的 STM32 HAL GPIO EXTI Adapter；
- 一个独占 SD 初始化、消抖、FatFs 卷生命周期和介质事件日志的 Storage Task；
- 一个只封装 FatFs 逻辑卷操作的 Filesystem Module；
- 一个由 CubeMX DiskIO 调用并桥接到 Platform SD 的同步 FatFs Bridge。

当前不抽象通用 `BlockDevice`。SD 是现阶段唯一的块设备；等 QSPI Flash 成为第二个真实实现，并明确其擦除、对齐和写入规则后，再从两个具体需求中提取公共接口更稳妥。

## 2. 目录与职责

```text
APP/
  app.c                         顶层启动编排和调度器启动前日志

Components/sd/
  sd.h                          可复用 Device 公共类型和接口
  sd.c                          状态机、参数校验、块访问和错误处理

Adapters/sd/
  sd_stm32_hal_adapter.h        STM32 HAL SDMMC Adapter 绑定接口
  sd_stm32_hal_adapter.c        STM32 HAL SD/GPIO 调用和状态转换

Adapters/gpio_exti/
  gpio_exti_stm32_hal_adapter.h 调用者持有的 GPIO EXTI 回调对象和注册接口
  gpio_exti_stm32_hal_adapter.c HAL GPIO EXTI 入口和回调链表分发

Platform/sd/
  platform_sd.h                 Storage 使用的 Platform 接口和检测通知回调类型
  platform_sd.c                 私有 Device、GPIO EXTI Callback、hsd1/SD_CD 装配

APP/tasks/storage/
  storage_task.c                FreeRTOS 通知、30 ms 消抖和调度循环
  storage_sd.h/.c               Platform SD 生命周期、FatFs 卷管理和介质日志

Service/filesystem/
  filesystem_service.c          FatFs 路径转换、挂载、卸载和格式化 Service 入口

FATFS/App/
  fatfs.c                       CubeMX Driver Link 与 BSP_SD_* 到 Platform SD 的桥接

Core/Inc/sdmmc.h
Core/Src/sdmmc.c                CubeMX 管理的 hsd1 和 HAL MSP 初始化/反初始化
```

`sd.c` 不得包含 `sdmmc.h`、`main.h` 或 STM32 HAL 头文件。具体 Adapter
可以认识 STM32 HAL 类型，但不固定使用 `hsd1` 或某个检测引脚。Platform 通过
`SDCard_STM32HALAdapterTypeDef` 装配 `hsd1`、SD_CD GPIO 和低有效电平，
并私有持有 Device 句柄，上层不能直接修改其状态。

## 3. 启动调用链

### 3.1 插卡启动

```text
app_init()
  -> Platform_Init()                          初始化整机强依赖设备
  -> app_task_start()
     -> Create Task
        -> LogService_Init()
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

## 4. 同步块读取链

```text
Platform_SD_ReadBlocks()
  -> SDCard_ReadBlocks()
     -> 校验指针、块数量和当前状态
     -> 以不会整数溢出的方式校验起始块和块数量
     -> State = BUSY
     -> Port.ReadBlocks()
        -> HAL_SD_ReadBlocks()
     -> Port.Sync()
        -> 轮询 HAL_SD_GetCardState() 直到 TRANSFER
     -> State = READY
```

写入使用相同结构。数据阶段完成后，SD 卡内部仍可能处于编程状态，因此写入后的 `Sync` 尤其重要：只有卡回到 `TRANSFER` 才能安全接受下一条命令。

## 5. 热插拔调用链

```text
EXTI9_5_IRQHandler()
  -> HAL_GPIO_EXTI_IRQHandler(SD_CD_Pin)
  -> HAL_GPIO_EXTI_Callback(SD_CD_Pin)
  -> GPIOEXTI_STM32HALAdapter 遍历回调链表
  -> PinMask 匹配 DetectIRQCallback
  -> platform_sd_detect_irq_cb(SD_CD_Pin, &hplatform_sd)
     -> storage_sd_callback(StorageTaskHandle)
        -> vTaskNotifyGiveFromISR()

Storage Task
  -> ulTaskNotifyTake(portMAX_DELAY) 取得边沿通知
  -> 以 30 ms 超时再次 ulTaskNotifyTake()
     -> 有新边沿：重新开始完整 30 ms 静默期
     -> 超时：Platform_SD_Process(&event)
        -> Platform_SD_Refresh()
        -> 仅在持续状态变化时产生 INSERTED 或 REMOVED
  -> Storage Task 根据 event 记录日志，并挂载或注销 FatFs 卷
```

### 5.1 为什么使用二值通知

卡座触点抖动可能产生多个边沿，但边沿次数没有业务意义。系统只关心“检测输入发生过变化，请在稳定后重新读取”。Storage Task 将任务通知视为重新开始消抖窗口的信号，不把通知计数解释为插拔次数。

### 5.2 为什么由 Platform SD 持有 GPIO EXTI Callback

GPIO EXTI Adapter 只认识 STM32 HAL 的 GPIO PinMask，不认识 SD、PMIC 或产品逻辑中断源。
`Platform_SD` 长期持有 `DetectIRQCallback`，初始化时注册、反初始化时注销，
回调上下文直接指回自己的私有 Handle。Platform 的原始 ISR 回调再调用
`Platform_SD_Init()` 注入的 `DetectCallback`；当前该回调由 Storage Task 的
`storage_sd_detect_callback()` 实现为
`vTaskNotifyGiveFromISR()`，但 Platform 本身不包含 FreeRTOS。

因此不再需要额外的 `Platform_IRQ_SourceTypeDef`、GPIO 到逻辑源的映射表或
Platform IRQ 二次分发。以后新增按键或 PMIC 中断时，由各自模块持有并注册新的
Callback 对象，GPIO EXTI Adapter 的实现不需要修改。

### 5.3 为什么使用直接任务通知

`volatile` 只保证每次都真实访问内存，不保证“读取并清除”这个复合操作不可被 ISR 打断。直接任务通知由 FreeRTOS 原子维护，且 Storage Task 是唯一接收者；它既没有额外队列存储，也不需要跨文件全局初始化标志。

ISR 只调用 `vTaskNotifyGiveFromISR()`，Storage Task 使用 `ulTaskNotifyTake()` 等待。每次收到新边沿后，任务重新等待完整的 30 ms；只有该时间内没有新通知，才执行后续刷新。

### 5.4 FreeRTOS 实现

当前实现中，`Platform_SD_Process()` 不再保存检测通知或主动等待；它只在消抖完成后调用 `Platform_SD_Refresh()` 并翻译 `INSERTED` / `REMOVED` 事件。Device 状态机、Port、错误模型和 Platform 公共接口都不依赖 FreeRTOS。

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
  |-- Read/Write/Sync -----------------> BUSY --> READY
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

如果 `SDCard_ReadBlocks()` 只转调 `HAL_SD_ReadBlocks()`，APP、FatFs DiskIO 和 USB MSC 都要重复这些规则，Device 就失去了存在价值。

## 9. CubeMX 边界

CubeMX 继续管理：

- `hsd1`；
- `HAL_SD_MspInit()`；
- `HAL_SD_MspDeInit()`；
- SDMMC1 的时钟和 GPIO 复用配置；
- EXTI IRQHandler 的生成代码。

当前不自动调用 `MX_SDMMC1_SD_Init()`。SD Card Port 直接调用有返回值的 `HAL_SD_Init()`，因为 CubeMX 生成的 `MX_SDMMC1_SD_Init()` 返回 `void`，失败时会进入 `Error_Handler()`，不适合可移除介质。

因此 `sd_stm32_hal_init()` 中保存了与 CubeMX 一致的以下参数：

- 时钟采样沿；
- 空闲时钟节能设置；
- 4 位总线宽度；
- 硬件流控；
- 时钟分频。

以后在 CubeMX 修改 SDMMC 配置时，必须同步核对该函数。自维护代码不应修改 CubeMX/ST 管理区；必须接入回调时，仅使用允许修改的 `USER CODE` 区。

## 10. FatFs 适配边界

当前 `FATFS/App/fatfs.c` 在 CubeMX 的 USER CODE 区强定义 `BSP_SD_*`，使生成的
`sd_diskio.c` 通过 Platform SD 访问介质，而不是直接使用 `hsd1`。映射关系为：

| FatFs DiskIO 操作 | Platform SD 接口 |
| --- | --- |
| `disk_initialize` | 检查已由 Storage Task 初始化完成的 `Platform_SD_GetState()` |
| `disk_status` | `Platform_SD_GetState()` 和 `Platform_SD_IsPresent()` |
| `disk_read` | `Platform_SD_ReadBlocks()` |
| `disk_write` | `Platform_SD_WriteBlocks()` |
| `CTRL_SYNC` | `Platform_SD_Sync()` |
| `GET_SECTOR_COUNT` | `Platform_SD_GetInfo()` 返回的 `BlockCount` |
| `GET_SECTOR_SIZE` | `Platform_SD_GetInfo()` 返回的 `BlockSize` |

文件系统挂载状态不属于 `SDCard_StateTypeDef`。`Filesystem` Module 不会调用
`Platform_SD_Init()`；Storage Task 必须先完成卡检测、消抖和 Platform 生命周期，
再调用挂载；拔卡后由同一任务注销 FatFs 卷对象。当前 Storage Task 是 SD 与本地
FatFs 的唯一普通任务上下文。未来若出现 `Service/storage`，它只应接收跨任务命令、
协调文件打开状态，并处理本地 FatFs 与 USB MSC 之间的介质所有权仲裁，而不应复制
Storage Task 的 Platform SD 生命周期实现。

## 11. 当前限制

1. 块传输使用 HAL 阻塞轮询，尚未实现 DMA 或中断传输；
2. 尚未制定 D-Cache 维护和 DMA 可访问缓冲区策略；
3. 当前热插拔路径依赖 FreeRTOS Storage Task；若要回到裸机，应在应用层提供轮询通知和消抖策略，而不是把 FreeRTOS 依赖加入 Platform；
4. 尚无 RTOS Mutex，同一 Device 句柄不能被多个执行上下文并发访问；
5. 传输和同步超时当前固定在 Device 实现中；
6. SD Card Port 的 SDMMC 初始化值必须与 CubeMX 配置保持一致；
7. 已实现本地 FatFs 挂载、卸载和显式格式化；尚未实现 USB MSC，以及本地 FatFs 与 USB MSC 之间的所有权切换。

## 12. 对外使用规则

应用代码通常只调用 `Platform_SD_*`。`SDCard_*` 接口用于 Platform 装配、Device 独立测试和未来替换 Port；`hsd1` 始终是 SD Card Port 的实现细节。

架构变化时，应同时更新本文和根目录的 `CONTEXT.md`，重点核对：

- 实际目录；
- 启动与热插拔调用链；
- 状态机；
- 错误和诊断边界；
- FreeRTOS 替换接缝；
- FatFs 映射；
- 当前限制。
