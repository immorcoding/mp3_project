# Platform Log

本 Module 选择并装配当前产品的日志输出后端和毫秒时间源。当前后端为 USB CDC Adapter。

## 公开 Interface

- `Platform_Log_Init()`：绑定输出与时间源，并初始化 `Components/log`。

## 编译期依赖与装配

- `LOG_Init()`；
- `LOG_UsbCDC_STM32HALAdapter_Bind()`。

## 运行时请求与事件路径

APP 在启动阶段经 `Platform_Log_Init()` 完成装配；后续 Service_Log/Log task 调用 Log Component，Component 经已绑定 USB Adapter 请求输出。USB 就绪与发送忙只由 Adapter 返回状态，不直接唤醒或调用业务 Task。

## 约束

- Adapter Context 必须具有静态生命周期，因为 USB 可能在函数返回后继续读取发送缓冲；
- 不调用 FreeRTOS queue，不负责异步消息块投递；
- 不让上层依赖 CDC 枚举、DTR 或端点状态。

## 命名

公开能力使用 `Platform_Log_*`；私有 Implementation 使用 `platform_log_*`。
