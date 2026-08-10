# Platform SD

本 Module 表示当前 PCB 唯一的 SD 卡槽。它私有装配 SD Card Device、SDMMC1 Adapter、卡检测 GPIO EXTI 节点和 SDMMC 传输 IRQ 节点，并向 Storage Task 发布轻量卡检测与传输事件。

## 公开 Interface

- `Platform_SD_Init()`、`Platform_SD_DeInit()`、`Platform_SD_Process()`：卡槽生命周期与卡检测刷新；
- `Platform_SD_StartReadBlocks()`、`Platform_SD_StartWriteBlocks()`：启动已装配的 DMA 块传输；
- `Platform_SD_CompleteTransfer()`：由 Storage Task 在 DMA 事件后提交或失败当前 Device 传输；
- `Platform_SD_GetState()`、`Platform_SD_GetInfo()`、`Platform_SD_IsPresent()`、`Platform_SD_GetDiagnostics()`；
- `Platform_SD_DetectCallbackTypeDef` 与 `Platform_SD_TransferCallbackTypeDef`：只用于将 ISR 轻量事件交给上层拥有的任务。

## 调用的 Interface

- `Components/sd` 的 Device Interface；
- `Adapters/sd` 的 STM32 SDMMC Port Bind Interface；
- `Adapters/irq` 的 GPIO EXTI、SDMMC 回调注册 Interface；
- 本板的 CubeMX `hsd1`、SD_CD GPIO 和引脚极性。

## 约束

- Platform 私有持有 Device、Adapter Context 和侵入式回调节点；Service/App 不得接触这些对象；
- 不包含 FreeRTOS，不执行消抖、FatFs、格式化、Cache 维护或任务通知；它只调用注入回调发布事件；
- 卡检测使用任务通知索引 0，DMA 完成使用索引 1 的约定属于 Storage/FatFs 上层，不属于本 Module；
- DMA 完成 IRQ 不是卡检测边沿，二者不可复用同一个无类型事件值。

跨层 Interface 使用 `Platform_SD_*`；本文件内的装配、映射和回调辅助实现使用 `platform_sd_*`。
