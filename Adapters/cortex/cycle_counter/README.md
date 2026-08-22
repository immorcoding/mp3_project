# Cortex-M7 Cycle Counter Adapter

本 Module 封装 Cortex-M7 `CoreDebug` 与 DWT `CYCCNT` 周期计数器，向普通上下文的
性能诊断提供一个稳定的架构计时能力。它不认识 FMC、SDRAM、DMA、任务或板级时钟策略。

## 公开 Interface

- `CortexM7CycleCounter_Start()`：复位并启动全局 32 位周期计数器；
- `CortexM7CycleCounter_Read()`：读取自上次启动后的当前周期数；
- `CortexM7CycleCounter_GetFrequencyHz()`：取得当前 Cortex-M7 核心频率。

## 编译期依赖

- CMSIS Core 提供的 `CoreDebug`、DWT、`SystemCoreClock`、`__DSB()` 与 `__ISB()`。

## 运行时请求与事件路径

Platform SDRAM 诊断在开始写入或冷读测量前调用 `Start()`，随后调用 `Read()` 计算周期
差值，再使用当前核心频率换算耗时和吞吐。本 Module 没有 IRQ、DMA 或任务通知路径。

## 资源与约束

- DWT `CYCCNT` 是整个内核共享的 32 位硬件资源，`Start()` 会清零已有计数；
- 仅限普通上下文串行使用，不能在 ISR 中与任务并发测量；
- 调用者应在一个计数器回绕周期内完成测量；以无符号减法计算短区间可自然处理一次回绕；
- 频率结果是 CPU `SystemCoreClock`，不是任何外设或外部存储器的时钟。

## 禁止依赖

- 不包含 STM32 HAL、CubeMX 外设 Handle 或 FreeRTOS；
- 不保存板级状态，不拥有 DMA、诊断策略、测试缓冲区或日志。
