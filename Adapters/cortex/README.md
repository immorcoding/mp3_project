# Cortex Adapter

本目录放置直接依赖 ARM Cortex-M 架构 Interface、但不依赖某个 STM32 HAL 外设实例的 Adapter。目前仅包含 Cortex-M7 D-Cache 维护能力；后续可放置同类的 MPU、FPU 或核心寄存器 Adapter。

## 公开 Interface

- 对上提供架构能力的窄 Interface，例如 DMA 缓冲区的 Cache 一致性维护；
- 公开的参数和类型不得携带具体 STM32 外设 Handle。

## 调用的 Interface

- CMSIS Core Interface，例如 `SCB_CleanDCache_by_Addr()`、`SCB_InvalidateDCache_by_Addr()` 与 `__DSB()`；
- 同目录内的私有辅助函数。

## 约束

- 不包含 STM32 HAL、CubeMX 外设 Handle 或 FreeRTOS；
- 不保存板级状态，不拥有 DMA 传输或任务通知；传输生命周期仍由具体外设 Adapter 和上层 Service 管理；
- 用于 DMA 的缓冲区仍由调用方保证可被 DMA 访问、按 Cache line 对齐，且在传输期间不被 CPU 并发读写。
