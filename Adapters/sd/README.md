# STM32 SD Adapter

本 Adapter 将 STM32 HAL SDMMC、卡检测 GPIO 和 HAL 状态转换为 SD Card Component 的 Port Ops。

## 公开 Interface

- `SDCard_STM32HALAdapter_Bind()`；
- `SDCard_STM32HALAdapterTypeDef`。

## 调用的 Interface

- `SDCard_*` Port Ops 和状态类型；
- `HAL_SD_*`、`HAL_GPIO_ReadPin()`。

## 约束

- Context 由 Platform 注入 SD Handle、检测 GPIO 与有效电平；
- 不固定使用 `hsd1`，不注册 EXTI，也不执行 FatFs 操作；
- 当前为同步轮询 Port；DMA 的启动、完成和同步语义必须作为独立 Interface 演进。
