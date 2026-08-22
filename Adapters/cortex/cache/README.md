# Cortex-M7 D-Cache Adapter

本目录封装 Cortex-M7 数据 Cache 的按范围维护。它是无状态 Adapter，可供 SDMMC、SPI 显示等 DMA Adapter，以及外部存储器诊断等非 DMA 调用者复用。

## 公开 Interface

- `CortexM7DCache_Clean_Aligned()`：严格按完整 Cache line 提交脏数据；
- `CortexM7DCache_Invalidate_Aligned()`：严格按完整 Cache line 使 CPU Cache 副本失效；
- `CortexM7DCache_CleanInvalidate_Aligned()`：严格按完整 Cache line 先提交脏数据，再使副本失效；
- `CortexM7DCache_Clean_Rounded()`：首地址对齐时向后覆盖至 Cache line 末尾后提交脏数据；
- `CORTEX_M7_DCACHE_LINE_SIZE`：Cortex-M7 D-Cache line 大小。

## 编译期依赖

- CMSIS Core 的 `SCB_*DCache_by_Addr()`；当前 CMSIS 实现已在操作内部完成必要的
  `__DSB()` 和 `__ISB()`，本 Module 不额外重复插入屏障。

## 运行时请求与事件路径

调用者按其资源语义选择维护时机：DMA Adapter 在传输启动或完成后调用，Platform SDRAM 诊断在需要强制访问外部存储器时调用。本 Module 不拥有 DMA、IRQ 回调或 FreeRTOS 通知。

## 约束

- 仅限普通任务上下文调用；
- `*_Aligned()` 要求传入非空、长度非零、首地址和长度均按 Cache line 对齐的 Cacheable 内存范围，并拥有该范围覆盖的全部 Cache line；
- `Clean_Rounded()` 仅允许用于 Clean：首地址必须按 Cache line 对齐，长度可不对齐；调用方必须拥有从首地址到向上补齐后末尾的完整范围；
- 不提供尾部补齐的 Invalidate 或 Clean + Invalidate，避免扩展 Cache line 时丢弃相邻脏数据；
- 不执行 DMA 启动、等待、完成回调注册或 FreeRTOS 通知。
