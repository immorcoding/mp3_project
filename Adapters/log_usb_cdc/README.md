# USB CDC Log Adapter

本 Adapter 将 USB CDC 的枚举、DTR、发送忙状态和异步缓冲约束转换为 Log Component 的输出与时间源 Interface。

## 公开 Interface

- `LOG_UsbCDC_STM32HALAdapter_Bind()`；
- USB CDC Adapter Context。

## 调用的 Interface

- `CDC_Transmit_FS()`、`CDC_IsReady_FS()`、`HAL_GetTick()`；
- Log Component 的输出 Ops 与时间源类型。

## 资源与约束

- Adapter 私有 TxBuffer 必须在 USB 异步发送完成前保持有效；
- USB 未就绪或忙属于可重试结果，不能让 Log Component 丢失队首；
- 不包含 FreeRTOS queue，也不负责多任务日志投递。
