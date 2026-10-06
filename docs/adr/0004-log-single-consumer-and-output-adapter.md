# ADR-0004：日志核心单消费者与可替换输出 Adapter

- 状态：已接受（按现有代码追溯记录）
- 日期：2026-08-29
- 相关实现说明：[../log_architecture.md](../shape/diagnostics.reference.md)

## 背景

启动期需要记录日志，而 USB CDC 的枚举、DTR 与异步发送均可能尚未就绪。调度器启动后，多个 Task 又不能并发操作无锁日志核心或复用仍被 USB 读取的发送缓冲。

## 决定

1. `Components/log` 拥有固定 RAM 队列、日志格式化和 `LOG_OutputOpsTypeDef`；它不选择 USB、UART 或其他后端。
2. `Adapters/stm32_hal/log_usb_cdc` 实现 USB CDC 输出与时间源；`Platform/log` 长期持有 Adapter Context 并完成绑定。
3. 调度器启动前允许 APP/Platform 直接投递到日志核心；调度器启动后，普通 Task 通过 `Service/log` 的静态消息块池投递，只有 Log Task 消费并调用日志核心。
4. ISR 不进行格式化、Service 日志投递或 USB 发送，只发布简短通知。

## 后果

- USB 未就绪时保留队首，启动日志可延后显示；
- USB 异步发送缓冲与日志核心队列分离，避免复用时覆盖在飞数据；
- 以后新增 UART/RTT 后端只新增 Adapter 并调整 Platform 装配，不修改日志核心。
