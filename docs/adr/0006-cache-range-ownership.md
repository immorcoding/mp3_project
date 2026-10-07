# ADR-0006：Cortex-M7 D-Cache 范围与 DMA 缓冲所有权

- 状态：已接受（按现有代码追溯记录）
- 日期：2026-08-29
- 相关实现说明：[../sdram_architecture.md](../shape/hardware.md)、[../sd_architecture.md](../shape/sources/storage-media.md)、[../../Adapters/cortex/cache/README.md](../../Adapters/cortex/cache/README.md)

## 背景

STM32H743 的 DMA 与 CPU D-Cache 不自动一致。对未拥有的相邻 Cache line 进行 Invalidate，可能丢弃其他对象的脏数据；而无约束的“自动对齐”接口会掩盖调用者的缓冲区所有权错误。

## 决定

1. `Adapters/cortex/cache` 只负责 D-Cache 范围维护，不负责 DMA 启动、等待或任务通知。
2. `Clean_Aligned()`、`Invalidate_Aligned()`、`CleanInvalidate_Aligned()` 要求首地址和长度均覆盖完整的 32 字节 Cache line，调用者必须拥有完整范围。
3. 仅为 DMA 读取 CPU 内存前的 Clean 提供 `Clean_Rounded()`：首地址仍必须对齐，末尾可向上补齐，调用者必须拥有补齐后的完整范围。
4. 不提供不严格范围的 Invalidate 或 Clean + Invalidate；专用 DMA 中转缓冲区应放在 DMA 可访问、明确对齐且无相邻对象共享的区域。

## 后果

- SDMMC、SPI DMA 与 SDRAM 诊断可复用同一 Cache Interface，但每个 Module 仍自行拥有传输生命周期；
- 对齐与缓冲区所有权成为显式调用前提，而非静默副作用；
- 未来直接 DMA 到其他内存前，必须重新验证 DMA 可达性、范围所有权与方向对应的 Cache 操作。
