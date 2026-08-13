# STM32 SD Adapter

本 Adapter 将 STM32 HAL SDMMC、卡检测 GPIO 和 HAL 状态转换为 SD Card Component 的 Port Ops。

## 公开 Interface

- `SDCard_STM32HALAdapter_Bind()`；
- `SDCard_STM32HALAdapterTypeDef`，其中保存注入的 SD Handle、卡检测 GPIO，以及当前 DMA 的 Cache 维护元数据。

## 编译期依赖

- `SDCard_*` Port Ops 和状态类型；
- `HAL_SD_*`、`HAL_GPIO_ReadPin()`；
- `Adapters/cortex/cache` 的 D-Cache 一致性维护 Interface。

## 运行时请求与事件路径

SD Card Device 经已绑定 PortOps 发起块访问时，本 Adapter 调用 HAL SDMMC；DMA 完成由独立 SDMMC IRQ Adapter 发布，随后由 Service 在任务上下文调用 `Sync()`，本 Adapter 不拥有通知等待。

## 约束

- Context 由 Platform 注入 SD Handle、检测 GPIO 与有效电平；
- 不固定使用 `hsd1`，不注册 EXTI，也不执行 FatFs 操作；
- `ReadBlocks()` / `WriteBlocks()` 保留阻塞轮询路径；`StartReadBlocks()` / `StartWriteBlocks()` 只启动 DMA，`Sync()` 在任务上下文确认传输结果并完成读取方向的 Cache 维护；
- 不拥有 DMA 等待、任务通知、超时策略或中转缓冲区；这些属于 Filesystem Service。
