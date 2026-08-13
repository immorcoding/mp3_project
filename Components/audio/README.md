# Audio Component

Audio Component 定义 PCM 发送和静音所需的 Bus/Ops 语义，不认识 STM32 I2S、PCM5102A GPIO 或播放任务。

## 公开 Interface

- `Audio_Init()`、`Audio_Transmit()`、`Audio_Mute()`；
- `Audio_HandleTypeDef`、`Audio_BusOpsTypeDef` 及其归一化状态。

## 编译期依赖

- 自身拥有的 Audio Bus 与 Mute Ops 类型。

## 运行时请求与事件路径

Audio Component 经 Ops 调用已绑定后端；Platform 负责绑定 Context。当前没有 Audio DMA 完成或播放 Task 事件路径。

## 约束

- Handle 与 Ops Context 的生命周期由 Platform 保证；
- 不直接访问 HAL 或传输 DMA；
- 不决定音频格式转换、缓冲或播放状态机。
