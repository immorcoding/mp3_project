# Diagnostics

错误解释与日志交付边界；通用错误模型沿用 [Architecture](architecture.md) ARC-9，ISR 边界沿用 ARC-10；调试中的易误判情况见 [错误观察对照](sources/diagnostic-observations.md)。

Next id: DIAG-3

## Pillars

- 一次调用结果不替代持续生命周期。
- 日志入队、被输出接受和主机显示分别判断。
- 诊断保留故障证据，不改变产品执行与缓冲所有权。

## logging

日志的并发接缝与异步缓冲寿命。

### Rules

- **DIAG-1** · settled · 调度器启动后只有 Log Task 消费无锁日志核心；其他普通任务经先初始化的 Service_Log 非阻塞投递，tag 使用静态存储期字符串，文本由消息块复制。_Why:_ 核心不支持并发，队列复制块指针而不延长 tag 生命周期。_Source:_ [ADR-0004](../adr/0004-log-single-consumer-and-output-adapter.md)、[Service Log](../../Service/log/README.md) _Check:_ independent-verifier 核对生产者、单消费者和消息所有权。
- **DIAG-2** · settled · 异步输出 Adapter 持有独立持久发送缓冲，提交后直到后端结束读取才复用；核心出队只表示 Adapter 已接受，不表示主机已收到。_Why:_ 出队后的核心槽位可立即复用，USB 仍异步读取提交指针。_Source:_ [USB 日志 Adapter](../../Adapters/stm32_hal/log_usb_cdc/README.md) _Check:_ embedded-reviewer 核对异步提交、完成与缓冲复用。

### References

- [Service Log](../../Service/log/README.md)：消息池所有权与任务接缝；[Log Component](../../Components/log/README.md)：核心队列；[USB Adapter](../../Adapters/stm32_hal/log_usb_cdc/README.md)：就绪和异步输出。
