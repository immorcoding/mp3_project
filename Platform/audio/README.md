# Platform Audio

本 Module 装配当前 PCB 的 I2S 输出、PCM 静音 GPIO 与 Audio Component，向上层提供音频发送与静音语义。

## 公开 Interface

- `Platform_Audio_Init()`；
- `Platform_Audio_Transmit()`；
- `Platform_Audio_SetMute()`。

## 编译期依赖与装配

- `Audio_*` 和 `AudioI2S_STM32HALAdapter_Bind()`；
- CubeMX `hi2s2`、PCM 静音 GPIO 定义。

## 运行时请求与事件路径

上层经 `Platform_Audio_*` 请求当前板的音频能力；本 Module 调用 Audio Component，Component 再经已绑定 I2S Adapter 到达 HAL。当前为阻塞发送，尚无音频 DMA/IRQ 事件路径。

## 约束

- Platform 私有持有 Audio Handle 与 Adapter Context；
- 不暴露 I2S HAL Handle，不持有播放缓冲区；
- DMA、Cache 和异步完成语义必须在实际引入播放 Module 时统一设计，不能在此伪造通用 Interface。

## 命名

公开能力使用 `Platform_Audio_*`；私有 Implementation 使用 `platform_audio_*`。
