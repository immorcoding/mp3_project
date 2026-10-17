# STM32 Audio I2S Adapter

本 Adapter 把 STM32 HAL I2S 循环 DMA 输出和 GPIO 静音控制转换为 Audio Component 的 Bus/Mute Ops。

## 公开 Interface

- `AudioI2S_STM32HALAdapter_Bind()`；
- `AudioI2S_STM32HALAdapterTypeDef`（含 ISR 事件处理函数与上下文槽）。

## 编译期依赖

- `Audio_*` 类型；
- `HAL_I2S_*`、`HAL_GPIO_*`、`DMA_Stream_TypeDef` 的 `CR.EN` 位；
- CMSIS `PRIMASK` 临界区原语。

## 运行时请求与事件路径

Audio Component 经已绑定 Bus/Mute Ops 发起 `Configure`（DeInit、改 `AudioFreq`、Init）、`StartCircular`（`HAL_I2S_Transmit_DMA`）、`Abort`（`HAL_I2S_DMAStop`）与 `IsStreamActive`（读 DMA 流 EN 位）。CubeMX 向量函数进入 HAL 后，本 Adapter 覆盖的 `HAL_I2S_TxHalfCpltCallback`、`HAL_I2S_TxCpltCallback`、`HAL_I2S_ErrorCallback` 把事件转成 `Audio_StreamEventTypeDef`，交给 Platform 经 Ops 安装的处理函数。

## 约束

- Context 由 Platform 持有并注入 I2S Handle、静音 GPIO 与极性；
- `USE_HAL_I2S_REGISTER_CALLBACKS` 为 0，本 Adapter 独占上述三个 HAL 全局回调符号；按 I2S Handle 匹配 Bind 时登记的唯一 Context；
- DMA 流（循环、半传输中断）与 NVIC 由 CubeMX 导出，本 Adapter 只经 `hdmatx` 在运行时使用，不引用生成的 DMA Handle 符号；`hdmatx` 为空时 `StartCircular` 失败；
- 不选择 `hi2s2`，不管理 PCM 缓冲区、Cache、计数或播放状态机；
- HAL 错误必须在 Adapter 内归一化为 Audio 状态。
