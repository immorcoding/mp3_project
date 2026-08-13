# STM32 HAL Adapter

本目录放置把 STM32 HAL、CubeMX 生成的外设 Handle 或 HAL 全局回调入口，转换为 Component 所属 Interface 的实现。具体 `hsd1`、`hi2s2`、GPIO 引脚和板级时序由 Platform 注入，不由本目录选择。

## 公开 Interface

- 各子目录声明的 `*_STM32HALAdapter_Bind()`、`*_Register()`、`*_Unregister()` 等装配或回调分发 Interface；
- 仅供 Platform 长期持有的 Adapter Context 类型。

当前 `st7789_spi/` 将 SPI 阻塞收发、CS/D-C/RESET GPIO 和 HAL 时基装配为 ST7789 Device 的 PortOps；它不包含显示初始化表、DMA 或 LVGL。

## 编译期依赖

- Component 定义的 Ops、状态和事件类型；
- STM32 HAL、CubeMX 生成的外设 Handle 与 HAL 回调约定；
- 同一 Adapter 内部的私有辅助函数。

## 约束

- 不包含或调用 `APP`、`Service`，不拥有任务、队列、通知或产品状态机；
- 不让 Component 反向依赖本目录；由 Component 声明 Ops Interface、由此处实现；
- `irq/` 只负责 HAL 全局回调的唯一所有权与源特定分发，具体业务只由已注册的上层回调在任务中处理。
