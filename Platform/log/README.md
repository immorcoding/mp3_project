# Platform Log

本 Module 选择并装配当前产品的日志输出后端和毫秒时间源。当前后端为 USB CDC Adapter。

## 公开 Interface

- `Platform_Log_Init()`：绑定输出与时间源，并初始化 `Components/log`。

## 调用的 Interface

- `LOG_Init()`；
- `LOG_UsbCDC_STM32HALAdapter_Bind()`。

## 约束

- Adapter Context 必须具有静态生命周期，因为 USB 可能在函数返回后继续读取发送缓冲；
- 不调用 FreeRTOS queue，不负责异步消息块投递；
- 不让上层依赖 CDC 枚举、DTR 或端点状态。

## 命名

公开能力使用 `Platform_Log_*`；私有 Implementation 使用 `platform_log_*`。
