# STM32 Audio I2S Adapter

本 Adapter 把 STM32 HAL I2S 发送和 GPIO 静音控制转换为 Audio Component 的 Bus/Mute Ops。

## 公开 Interface

- `AudioI2S_STM32HALAdapter_Bind()`；
- `AudioI2S_STM32HALAdapterTypeDef`。

## 编译期依赖

- `Audio_*` 类型；
- `HAL_I2S_*`、`HAL_GPIO_*`。

## 运行时请求与事件路径

Audio Component 经已绑定 Bus/Mute Ops 发起请求时，本 Adapter 调用 HAL；当前没有 DMA 完成或播放 Task 事件路径。

## 约束

- Context 由 Platform 持有并注入 I2S Handle、静音 GPIO 与极性；
- 不选择 `hi2s2`，不管理 PCM 缓冲区、DMA 或播放状态机；
- HAL 错误必须在 Adapter 内归一化为 Audio 状态。
