# Cortex-M7 D-Cache Adapter

本目录封装 Cortex-M7 数据 Cache 的按范围维护。它是无状态 Adapter，可供 SDMMC、SPI 显示等 DMA Adapter，以及外部存储器诊断等非 DMA 调用者复用。

## 公开 Interface

- `CortexM7DCache_CleanRange()`：提交指定 Cacheable 范围中的脏数据；
- `CortexM7DCache_InvalidateRange()`：使指定范围的 CPU Cache 副本失效；
- `CortexM7DCache_CleanInvalidateRange()`：先提交脏数据，再使指定范围失效；
- `CORTEX_M7_DCACHE_LINE_SIZE`：Cortex-M7 D-Cache line 大小。

## 编译期依赖

- CMSIS Core 的 `SCB_*DCache_by_Addr()` 和 `__DSB()`。

## 运行时请求与事件路径

调用者按其资源语义选择维护时机：DMA Adapter 在传输启动或完成后调用，Platform SDRAM 诊断在需要强制访问外部存储器时调用。本 Module 不拥有 DMA、IRQ 回调或 FreeRTOS 通知。

## 约束

- 仅限普通任务上下文调用；
- 调用方必须传入非空、长度非零、首地址和长度均按 Cache line 对齐的 Cacheable 内存范围，并拥有该范围覆盖的全部 Cache line；
- 不执行 DMA 启动、等待、完成回调注册或 FreeRTOS 通知。
