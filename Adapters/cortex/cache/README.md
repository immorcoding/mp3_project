# Cortex-M7 D-Cache Adapter

本目录封装 Cortex-M7 数据 Cache 与 DMA 的一致性维护。它是无状态 Adapter，可供 SDMMC、SPI 显示等任何 DMA 外设 Adapter 复用。

## 公开 Interface

- `CortexM7DCache_PrepareDMARx()`：外设写入 RAM 前执行 Clean + Invalidate；
- `CortexM7DCache_CompleteDMARx()`：外设写入 RAM 完成后执行 Invalidate；
- `CortexM7DCache_PrepareDMATx()`：RAM 数据交给外设读取前执行 Clean；
- `CORTEX_M7_DCACHE_LINE_SIZE`：Cortex-M7 D-Cache line 大小。

## 编译期依赖

- CMSIS Core 的 `SCB_*DCache_by_Addr()` 和 `__DSB()`。

## 运行时请求与事件路径

外设 Adapter 在 DMA 启动前或任务上下文确认完成后调用本 Module；它不拥有 DMA、IRQ 回调或 FreeRTOS 通知。

## 约束

- 仅限普通任务上下文调用；
- 调用方必须传入非空、长度非零、首地址和长度均按 Cache line 对齐的 DMA 缓冲区；
- 不执行 DMA 启动、等待、完成回调注册或 FreeRTOS 通知。
