# USB CDC Log Adapter

本 Adapter 将 USB CDC 的枚举、DTR、发送忙状态和异步缓冲约束转换为 Log Component 的输出与时间源 Interface。

## 公开 Interface

- `LOG_UsbCDC_STM32HALAdapter_Bind()`；
- USB CDC Adapter Context。

## 编译期依赖

- `CDC_Transmit_FS()`、`CDC_IsReady_FS()`、`HAL_GetTick()`；
- Log Component 的输出 Ops 与时间源类型。

## 运行时请求与事件路径

Log Component 经已绑定 Ops 请求输出时，本 Adapter 调用 CDC；USB 就绪或忙状态只返回归一化结果，Log task 随后重试。Adapter 不直接调用 LogService 或任务。

## 资源与约束

- Adapter 私有 TxBuffer 必须在 USB 异步发送完成前保持有效；
- USB 未就绪或忙属于可重试结果，不能让 Log Component 丢失队首；
- 不包含 FreeRTOS queue，也不负责多任务日志投递。
