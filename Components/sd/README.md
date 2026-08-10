# SD Card Component

SD Card Component 是面向逻辑块的可复用介质状态机。它维护生命周期、容量缓存、块范围检查、同步、DMA 传输状态和归一化错误；它不认识 SDMMC、GPIO、FreeRTOS 或 FatFs。

## 公开 Interface

- `SDCard_Init()`、`SDCard_DeInit()`、`SDCard_Refresh()`；
- `SDCard_ReadBlocks()`、`SDCard_WriteBlocks()`：阻塞轮询路径；
- `SDCard_StartReadBlocks()`、`SDCard_StartWriteBlocks()`：只启动 DMA，并进入 `BUSY`；
- `SDCard_CompleteTransfer()`、`SDCard_FailTransfer()`：由普通上下文在传输事件后推进状态；
- `SDCard_GetInfo()`、`SDCard_GetState()`、`SDCard_GetDiagnostics()`；
- `SDCard_PortOpsTypeDef`：由消费方定义、由具体 Adapter 实现的端口契约。

## 调用的 Interface

- 调用注入的 `SDCard_PortOpsTypeDef`；
- 仅使用 ISO C，不包含 HAL、Adapter、RTOS 或 FatFs Interface。

## 约束

- Start 接口返回成功不代表 DMA 完成；同一 Device 同时只允许一笔 `BUSY` 传输；
- `CompleteTransfer()` / `FailTransfer()` 不能从 ISR 调用；ISR 只应通知上层任务；
- 轮询与 DMA 路径都保留：前者便于启动诊断和低复杂度场景，后者由调用方负责等待完成事件与 Cache 一致性；
- 不拥有卡检测引脚、HAL Handle、DMA 缓冲区或文件系统挂载状态。

跨 Module Interface 使用 `SDCard_*`，状态机与参数检查辅助函数使用 `sdcard_*`。
