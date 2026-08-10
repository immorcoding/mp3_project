# Audio Component

Audio Component 定义 PCM 发送和静音所需的 Bus/Ops 语义，不认识 STM32 I2S、PCM5102A GPIO 或播放任务。

## 公开 Interface

- `Audio_Init()`、`Audio_Transmit()`、`Audio_Mute()`；
- `Audio_HandleTypeDef`、`Audio_BusOpsTypeDef` 及其归一化状态。

## 调用的 Interface

- Platform 注入的 Audio Bus 与 Mute Ops。

## 约束

- Handle 与 Ops Context 的生命周期由 Platform 保证；
- 不直接访问 HAL 或传输 DMA；
- 不决定音频格式转换、缓冲或播放状态机。
