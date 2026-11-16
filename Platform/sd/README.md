# Platform SD 模块

本 Module 表示当前产品 PCB 上唯一的 SD 卡槽。Implementation 负责装配 SD Card Device、STM32 SDMMC Adapter、卡检测 GPIO EXTI 节点与 SDMMC IRQ 节点；它向上提供板级 SD 语义，不暴露 `hsd1`、GPIO 引脚、HAL 类型或 FreeRTOS。

## 公开 Interface

- `Platform_SD_Init()`、`Platform_SD_DeInit()`、`Platform_SD_Process()`：管理卡槽生命周期并刷新已经消抖的卡检测状态。`Platform_SD_Process()` 的拔出事件按“最近一次告诉上层的状态”（Init 之后、每次成功的 Process 之后记下）判断，而不是按入口时 Device 的状态：读写中途拔卡时，`SDCard_FailTransfer`、`SDCard_AbortTransfer` 或被拒的启动会在 Process 之前就把 Device 写成 NOT_PRESENT，按入口状态比较会吞掉拔出，卷任务不会收到拔出事件，卷不卸载、被占用的文件槽不释放，重新插卡时挂载返回 BUSY；按已报告状态比较，READY → NOT_PRESENT 无论发生在 Process 里还是之前都恰好报告一次 REMOVED。插入事件仍按入口状态判断：静默拔出之后卡在 Process 之前又插回，看到 NOT_PRESENT → READY，报告 INSERTED（卷任务的对账视同换卡，走 `remount`）。**调用者不再是 APP**：自 0.10.0 起 APP 不包含 `platform_sd.h`，`Platform_SD_Init/Process` 只经 Filesystem Service 的 `StartSD`/`RefreshSD` 在 SD 卷锁内调用（设备状态由卷锁保护，APP 不绕过；原因见 [Filesystem Service](../../Service/filesystem/README.md) 的“为什么刷新和启动必须持 SD 卷锁”），由卷任务（[`APP/tasks/volume/`](../../APP/tasks/volume/README.md)）在消抖之后调用；卡检测回调由卷任务提供、在 ISR 里只发一个任务通知。
- `Platform_SD_SetTransferCallback()`、`Platform_SD_ClearTransferCallback()`：管理唯一的、ISR 安全的 SDMMC 传输事件订阅者；这是单槽位，不是回调链表。
- `Platform_SD_StartReadBlocks()`、`Platform_SD_StartWriteBlocks()`：启动已装配的 DMA 块传输。
- `Platform_SD_CompleteTransfer()`：在任务上下文提交已完成、失败或中止的 Device 传输。
- `Platform_SD_AbortTransfer(result)`：等传输完成事件超时后，在上界内显式中止在飞的 DMA（#209），`result` 报告 `STOPPED`（Device 回到 READY，可立即再传）、`NO_MEDIA`（卡已拔出，SDMMC 已反初始化）、`STUCK`（上界内没停住，SDMMC 已反初始化，Device 保持 ERROR）或 `REJECTED`（没有受理：Device 不在 BUSY，状态不变；或 Port 没有安装中止操作，Device 进入 ERROR）；返回 `PLATFORM_OK` 当且仅当 `STOPPED`。上界和耗时的细节在 [SD Card Component](../../Components/sd/README.md) 与 [STM32 SD Adapter](../../Adapters/stm32_hal/sd/README.md)。
- `Platform_SD_GetState()`、`Platform_SD_IsPresent()`、`Platform_SD_GetInfo()`、`Platform_SD_GetDiagnostics()`：提供稳定的板级状态观察。

## 编译期依赖与装配

- `Components/sd` 的逻辑块 Device Interface。
- STM32 SDMMC Port Adapter 以及 GPIO EXTI / SDMMC IRQ Adapter 的注册 Interface。
- CubeMX 持有的 `hsd1`、`SD_CD_GPIO_Port`、`SD_CD_Pin` 和卡检测极性。

## 运行时请求与事件路径

上层通过 `Platform_SD_*` 请求卡槽能力；本 Module 再调用 SD Card Device，后者经已绑定 Adapter 访问硬件。GPIO EXTI 和 SDMMC IRQ 分别由 Adapter 以源特定回调进入本 Module；本 Module 只转发卡检测或传输事件给已注册上层，不解释 FreeRTOS 通知索引或业务策略。

## 约束

- Platform Implementation 私有持有 Device、Adapter Context 和侵入式 IRQ 节点；Service 与 APP 不得访问它们。
- Platform 不包含 FreeRTOS，也不调用 `xTaskNotify...`、FatFs、Cache 维护函数或日志。
- Filesystem Service 在 Platform 初始化后设置传输订阅者；Platform 的 IRQ 转发回调只转换并转交强类型事件。
- 卡检测和传输完成是不同事件源。Platform 不分配 FreeRTOS 通知索引，也不创建信号量；卡检测的通知槽属于 APP，传输完成的信号量属于 Filesystem Service。
- `Platform_SD_AbortTransfer()` 返回前，Device 不再是 READY 时同时注销 SDMMC 传输 IRQ 节点（与 `Platform_SD_Refresh()` 同一规则）：`STUCK` 之后 HAL 回调指针会在重新初始化时被复位，节点若仍显示“已注册”就不会再注册，之后每笔传输都会等不到完成事件。
- `Platform_SD_ClearTransferCallback()` 仅用于确认没有 DMA 在飞时的真实运行时回收；正常拔卡不应清除订阅者，否则下次插卡不能自动恢复 SDMMC IRQ Adapter。

## 命名

跨层 Interface 使用 `Platform_SD_*`；本目录的装配、映射和 IRQ 转发 Implementation 使用 `platform_sd_*`。
