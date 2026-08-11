# Platform SD 模块

本 Module 表示当前产品 PCB 上唯一的 SD 卡槽。Implementation 负责装配 SD Card Device、STM32 SDMMC Adapter、卡检测 GPIO EXTI 节点与 SDMMC IRQ 节点；它向上提供板级 SD 语义，不暴露 `hsd1`、GPIO 引脚、HAL 类型或 FreeRTOS。

## 公开 Interface

- `Platform_SD_Init()`、`Platform_SD_DeInit()`、`Platform_SD_Process()`：管理卡槽生命周期并刷新已经消抖的卡检测状态。
- `Platform_SD_SetTransferCallback()`、`Platform_SD_ClearTransferCallback()`：管理唯一的、ISR 安全的 SDMMC 传输事件订阅者；这是单槽位，不是回调链表。
- `Platform_SD_StartReadBlocks()`、`Platform_SD_StartWriteBlocks()`：启动已装配的 DMA 块传输。
- `Platform_SD_CompleteTransfer()`：在任务上下文提交已完成、失败或中止的 Device 传输。
- `Platform_SD_GetState()`、`Platform_SD_IsPresent()`、`Platform_SD_GetInfo()`、`Platform_SD_GetDiagnostics()`：提供稳定的板级状态观察。

## 调用的 Interface

- `Components/sd` 的逻辑块 Device Interface。
- STM32 SDMMC Port Adapter 以及 GPIO EXTI / SDMMC IRQ Adapter 的注册 Interface。
- CubeMX 持有的 `hsd1`、`SD_CD_GPIO_Port`、`SD_CD_Pin` 和卡检测极性。

## 约束

- Platform Implementation 私有持有 Device、Adapter Context 和侵入式 IRQ 节点；Service 与 APP 不得访问它们。
- Platform 不包含 FreeRTOS，也不调用 `xTaskNotify...`、FatFs、Cache 维护函数或日志。
- Filesystem Service 在 Platform 初始化后设置传输订阅者；Platform 的 IRQ 转发回调只转换并转交强类型事件。
- 卡检测和传输完成是不同事件源。Platform 不分配 FreeRTOS 通知索引；索引 `0` 与索引 `1` 属于上层 Module 的职责。
- `Platform_SD_ClearTransferCallback()` 仅用于确认没有 DMA 在飞时的真实运行时回收；正常拔卡不应清除订阅者，否则下次插卡不能自动恢复 SDMMC IRQ Adapter。

## 命名

跨层 Interface 使用 `Platform_SD_*`；本目录的装配、映射和 IRQ 转发 Implementation 使用 `platform_sd_*`。
