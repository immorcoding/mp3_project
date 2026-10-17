# Platform Audio

本 Module 装配当前 PCB 的 I2S2 循环 DMA 输出、PCM 静音 GPIO 与 Audio Component，向上层提供“循环缓冲区输出 + 半区完成事实 + 静音编排”语义。

## 公开 Interface

- `Platform_Audio_Init()`；
- `Platform_Audio_SetEventCallback()`、`Platform_Audio_ClearEventCallback()`：单槽回调，ISR 中调用，只唤醒；
- `Platform_Audio_Start()`、`Platform_Audio_SubmitHalf()`、`Platform_Audio_Stop()`；
- `Platform_Audio_GetStatus()`，返回 `Platform_Audio_StatusTypeDef`（`ReleasedHalves`、`Underruns`、`Fault`）；
- `Platform_Audio_FaultTypeDef`：`NONE`、`TRANSFER_ERROR`、`STOP_FAILED`（锁存，重启前不解除）。

## 编译期依赖与装配

- `Audio_*` 和 `AudioI2S_STM32HALAdapter_Bind()`；
- CubeMX `hi2s2`、PCM 静音 GPIO 定义；
- `CortexM7DCache_Clean_Aligned()`（Start 清两个半区，SubmitHalf 清一个半区）；
- I2S2 的 DMA 流（循环、半传输中断）与 NVIC 由 CubeMX 导出，经 `hi2s2.hdmatx` 在运行时使用；未导出时 Start 返回 `PLATFORM_AUDIO_ERROR`，不影响编译。

## 运行时请求与事件路径

调用者持有环形缓冲区，分两个等长半区。`Start` 重新 Init I2S（PLL3 不动）、视两个半区为已提交并 Clean、启动 DMA、等 DAC 就绪后解除静音。DMA 每读完一个半区，Adapter 的 HAL 回调经 Audio Component 事件 Ops 进入本 Module：累加 `ReleasedHalves`，另一半区未提交则 `Underruns` 加一，随后调用单槽回调唤醒调用者；调用者经 `GetStatus` 读取事实，补满释放的半区并 `SubmitHalf`。第 n 次释放的是半区 (n−1) 的奇偶：奇数次为半区 0，偶数次为半区 1。

`Stop` 在调用者上下文同步执行：软静音并等斜坡，Abort，核实 DMA 流 EN 位为 0（HAL 返回码不作判据）；失败则重新 Init I2S 与 DMA 后重试一次，仍失败锁存 `STOP_FAILED`，此后 `Start` 一律拒绝。`Stop` 返回 OK 后不再回调，计数冻结至下一次 `Start`。DMA 传输错误只记 `TRANSFER_ERROR` 并唤醒，由调用者调用 `Stop`，不自动重试。

## 约束

- Platform 私有持有 Audio Handle、Adapter Context、计数与提交标志；回调在 ISR 中运行，只能执行 FromISR 安全的轻量操作；
- 两种 Start 拒绝（锁存、参数或采样率不被接受）都返回 `PLATFORM_AUDIO_ERROR`，调用者读 `GetStatus().Fault` 区分；`Platform_StatusTypeDef` 不新增值；
- 仅支持 32000、44100、48000 Hz；缓冲区首地址与半区字节数必须按 D-Cache 行对齐；
- XSMT 只由本 Module 在 Start/Stop 内编排，不再公开 `SetMute`，也没有 Pause、Resume、Recover；
- 等待用 `HAL_Delay`（斜坡 4 ms、DAC 就绪 3 ms，常量在 `platform_audio.c`），不含 FreeRTOS；Start 阻塞加一次补满半区须小于一个半区周期，超标时先把每半区帧数改为 2304；
- 不暴露 I2S HAL Handle，缓冲区所有权归调用者（`STOP_FAILED` 时缓冲留在 Platform，不得再写）；
- 斜坡与 DAC 就绪时长、Stop 回调顺序、Abort 耗时与实际 Fs 为板级项，`hw:pending`。

## 命名

公开能力使用 `Platform_Audio_*`；私有 Implementation 使用 `platform_audio_*`。
