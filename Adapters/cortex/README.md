# Cortex Adapter

本目录放置直接依赖 ARM Cortex-M 架构 Interface、但不依赖某个 STM32 HAL 外设实例的 Adapter。目前包含 Cortex-M7 D-Cache 维护和 DWT 周期计数能力；后续可放置同类的 MPU、FPU 或核心寄存器 Adapter。

## 公开 Interface

- 对上提供架构能力的窄 Interface，例如 Cacheable 内存范围的 D-Cache 维护和周期计数；
- 公开的参数和类型不得携带具体 STM32 外设 Handle。

## 编译期依赖

- CMSIS Core Interface，例如 `SCB_CleanDCache_by_Addr()`、`SCB_InvalidateDCache_by_Addr()` 与 `__DSB()`；
- 同目录内的私有辅助函数。

## 运行时请求与事件路径

具体外设 Adapter 与 Platform 诊断 Module 在普通上下文调用 Cortex Adapter 维护 Cache 或读取周期数；本目录不启动 DMA、不接收 IRQ，也不拥有任务事件。

## 约束

- 不包含 STM32 HAL、CubeMX 外设 Handle 或 FreeRTOS；
- 不保存板级状态，不拥有 DMA 传输或任务通知；传输生命周期仍由具体外设 Adapter 和上层 Service 管理；
- 用于 DMA 的缓冲区仍由调用方保证可被 DMA 访问、按 Cache line 对齐，且在传输期间不被 CPU 并发读写；非 DMA 调用者也必须拥有整个维护范围，避免操作相邻变量所在的 Cache line。
