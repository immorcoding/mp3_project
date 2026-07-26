# SD 卡子系统架构

> 适用工程：`version0.1.2`
>
> 当前后端：STM32H743 SDMMC1，4 位总线
>
> 当前调度：FreeRTOS 已接入；SD 热插拔仍保留可替换的轻量通知接缝
>
> 当前范围：逻辑块访问和热插拔，不包含 FatFs

## 1. 设计目的

本文记录接入 FatFs 之前的可移除 SD 卡栈。当前实现包含：

- 一个可复用、面向逻辑块的 SD Card Device；
- 一个绑定 STM32H743 SDMMC1 的具体 Port；
- 一个代表本板唯一物理卡槽的 Platform 接口；
- 一个把 GPIO EXTI 映射为逻辑中断源的 Platform IRQ 分发器；
- APP 对“可选介质”的启动和日志策略。

当前不抽象通用 `BlockDevice`。SD 是现阶段唯一的块设备；等 QSPI Flash 成为第二个真实实现，并明确其擦除、对齐和写入规则后，再从两个具体需求中提取公共接口更稳妥。

## 2. 目录与职责

```text
APP/
  app.c                         可选介质启动策略、事件处理和日志

Components/sd/
  sd.h                          可复用 Device 公共类型和接口
  sd.c                          状态机、参数校验、块访问和错误处理

Adapters/sd_stm32_hal/
  sd_stm32_hal_adapter.h        STM32 HAL SDMMC Adapter 绑定接口
  sd_stm32_hal_adapter.c        STM32 HAL SD/GPIO 调用和状态转换

Platform/sd/
  platform_sd.h                    APP/未来 Storage 使用的 Platform 接口
  platform_sd.c                    私有 Device、hsd1/SD_CD 装配、消抖和事件映射

Platform/irq/
  platform_irq.h                   逻辑中断源注册与分发接口
  platform_irq.c                   GPIO 引脚到逻辑中断源的映射

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
  -> app_init_sd()                         初始化可选介质
     -> Platform_SD_Init()
        -> SDCard_STM32HALAdapter_Bind(&Device, &Adapter)
        -> Platform_IRQ_Register(SD_DETECT)   注册卡槽私有 ISR 回调
        -> SDCard_Init(&Device)
           -> Port.IsPresent(PC7)
           -> Port.Init(&hsd1)
              -> HAL_SD_Init(&hsd1)
                 -> CubeMX 生成的 HAL_SD_MspInit()
           -> Port.GetInfo(&hsd1)
           -> 缓存归一化逻辑块信息
           -> State = READY
```

初始化成功后，APP 通过 `Platform_SD_GetInfo()` 取得副本并打印容量，不直接接触私有 Device 句柄或 `hsd1`。

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

没有插卡是正常的持续状态，不是整机故障。因此 APP 可以继续启动。之后若在 `NOT_PRESENT` 状态读取逻辑块或获取卡信息，因为该次操作无法完成，函数仍会返回失败。

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
  -> Platform_IRQ_DispatchFromISR(PLATFORM_IRQ_SOURCE_SD_DETECT)
  -> platform_sd_detect_irq_cb()
     -> DetectPending = true

app_run()
  -> app_process_sd()
     -> Platform_SD_Process(&event)
        -> 原子取得并清除 DetectPending
        -> 记录 DebounceStartMs，进入 Debouncing
        -> 30 ms 内收到新边沿时重新开始计时
        -> 连续安静满 30 ms 后调用 Platform_SD_Refresh()
        -> 仅在持续状态变化时产生 INSERTED 或 REMOVED
  -> APP 根据 event 记录日志
```

### 5.1 为什么使用二值通知

卡座触点抖动可能产生多个边沿，但边沿次数没有业务意义。系统只关心“检测输入发生过变化，请在稳定后重新读取”。因此 `DetectPending` 是二值通知，不记录插拔次数。

### 5.2 为什么不能只依赖 `volatile`

`volatile` 只保证每次都真实访问内存，不保证“读取并清除”这个复合操作不可被 ISR 打断。如果普通上下文读取到 `true` 后 ISR 再次写入 `true`，随后普通上下文清零，就可能丢掉新通知。

`platform_sd_take_detect_event()` 使用极短的 PRIMASK 临界区原子地取得并清除通知，并恢复调用前的全局中断状态。快速路径会先判断无通知并直接返回，避免主循环每轮都短暂关中断。

### 5.3 FreeRTOS 替换接缝

`platform_sd.c` 中带有“FreeRTOS 移植替换区”的代码是调度接缝。接入 RTOS 后建议：

- ISR 中用 `vTaskNotifyGiveFromISR()` 通知 Storage Task；
- Storage Task 收到通知后启动或重新启动 30 ms 消抖等待；
- 消抖结束后仍调用相同的 `Platform_SD_Refresh()`；
- Platform 的 `INSERTED` / `REMOVED` 事件语义保持不变。

Device 状态机、Port、错误模型和 Platform 公共接口不需要因为调度方式变化而重写。

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

`SDCard_Refresh()` 不负责机械触点消抖，也不能从 EXTI ISR 调用。当前由 `Platform_SD_Process()` 在普通上下文完成可重新开始的 30 ms 消抖，然后再调用 Refresh。

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

## 10. 未来 FatFs 适配

当前版本不实现 FatFs。未来 DiskIO Adapter 可以这样映射：

| FatFs DiskIO 操作 | Platform SD 接口 |
| --- | --- |
| `disk_initialize` | 稳定检测后调用 `Platform_SD_Init()` 或 `Platform_SD_Refresh()` |
| `disk_status` | `Platform_SD_GetState()` 和 `Platform_SD_IsPresent()` |
| `disk_read` | `Platform_SD_ReadBlocks()` |
| `disk_write` | `Platform_SD_WriteBlocks()` |
| `CTRL_SYNC` | `Platform_SD_Sync()` |
| `GET_SECTOR_COUNT` | `Platform_SD_GetInfo()` 返回的 `BlockCount` |
| `GET_SECTOR_SIZE` | `Platform_SD_GetInfo()` 返回的 `BlockSize` |

文件系统挂载状态不属于 `SDCard_StateTypeDef`。挂载/卸载、打开文件失效处理，以及本地 FatFs 与 USB MSC 之间的介质所有权仲裁，应由未来的 `Services/storage` 模块负责。

## 11. 当前限制

1. 块传输使用 HAL 阻塞轮询，尚未实现 DMA 或中断传输；
2. 尚未制定 D-Cache 维护和 DMA 可访问缓冲区策略；
3. 裸机阶段需要主循环频繁调用 `Platform_SD_Process()`，以后由 Storage Task 的任务通知替代；
4. 尚无 RTOS Mutex，同一 Device 句柄不能被多个执行上下文并发访问；
5. 传输和同步超时当前固定在 Device 实现中；
6. SD Card Port 的 SDMMC 初始化值必须与 CubeMX 配置保持一致；
7. 尚未实现 FatFs、USB MSC 以及二者之间的所有权切换。

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
