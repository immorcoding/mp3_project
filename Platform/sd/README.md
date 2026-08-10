# Platform SD

本 Module 表示当前 PCB 唯一的 SD 卡槽。它装配 SD Card Device、SDMMC1 Adapter 和卡检测 GPIO EXTI 节点，并向 Storage task 发布轻量状态变化通知。

## 公开 Interface

- 生命周期与事件：`Platform_SD_Init()`、`Platform_SD_DeInit()`、`Platform_SD_Process()`、`Platform_SD_GetState()`；
- 信息与诊断：`Platform_SD_GetInfo()`、`Platform_SD_GetDiagnostics()`、`Platform_SD_IsPresent()`；
- 块访问：`Platform_SD_ReadBlocks()`、`Platform_SD_WriteBlocks()`、`Platform_SD_Sync()`。

## 调用的 Interface

- `SDCard_*`；
- `SDCard_STM32HALAdapter_Bind()`；
- GPIO EXTI Adapter 注册 Interface；
- CubeMX `hsd1`、SD_CD GPIO 定义。

## 资源与约束

- 私有持有 Device、Adapter Context 和侵入式 EXTI 回调节点；
- 检测回调在 ISR 上下文，仅允许常数时间通知；
- 不包含 FreeRTOS，也不在此处理消抖、FatFs 或格式化；
- 上层不得接触 `hsd1` 或修改 Device 状态。

## 命名

公开能力使用 `Platform_SD_*`；私有 Implementation 使用 `platform_sd_*`。
