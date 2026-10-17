# Audio Component

Audio Component 定义 PCM 循环输出和静音所需的 Bus/Ops 语义，不认识 STM32 I2S、PCM5102A GPIO 或播放任务。

## 公开 Interface

- `Audio_Init()`、`Audio_Configure()`、`Audio_StartCircular()`、`Audio_Abort()`、`Audio_IsStreamActive()`、`Audio_SetStreamEventHandler()`、`Audio_Mute()`；
- `Audio_HandleTypeDef`、`Audio_BusOpsTypeDef`（`Prepare`、`Configure`、`StartCircular`、`Abort`、`IsStreamActive`、`SetEventHandler`）及其归一化状态；
- `Audio_StreamEventTypeDef`（半区 0 释放、半区 1 释放、错误）与 `Audio_StreamEventFunc`。

同步 `Transmit` Bus Op 已删除。

## 编译期依赖

- 自身拥有的 Audio Bus 与 Mute Ops 类型。

## 运行时请求与事件路径

Audio Component 经 Ops 调用已绑定后端；Platform 负责绑定 Context。后端在 ISR 上下文经 `Audio_StreamEventFunc` 报告半区释放与错误，Component 本身不缓存事件，也不决定谁来消费。

## 约束

- Handle 与 Ops Context 的生命周期由 Platform 保证；
- `Audio_Abort()` 的返回值不是停止判据，调用者须再用 `Audio_IsStreamActive()` 核实 DMA 流；
- 不直接访问 HAL 或传输 DMA；
- 不决定音频格式转换、缓冲、计数、故障锁存或播放状态机。
