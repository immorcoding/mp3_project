# LogService

LogService 将多个普通任务的短日志文本汇集到静态消息块池，并由 Log task 顺序转交给 `Components/log`。它解决多任务生产与单一输出消费者之间的同步，不取代日志核心的格式化和 USB 输出能力。

## 公开 Interface

- `LogService_Init()`：创建 free queue、ready queue 并归入全部静态消息块；
- `LogService_Post()`：非阻塞投递一条已格式化的文本；
- `LogService_Consume()`：由 Log task 推进输出，并至多转交一个 ready 消息。

## 调用的 Interface

- `LOG_Process()`、`LOG_Printf()`、`LOG_GetStats()`；
- FreeRTOS queue Interface。

## 资源与约束

- 队列元素是 `log_service_message_t *` 的指针值，不是完整结构体副本；
- 所有权路径固定为 `free → producer → ready → Log task → free`；
- `tag` 必须在消费前保持有效，通常使用字符串字面量；
- 仅普通任务可调用，不支持 ISR；队列满时不等待并返回错误。

## 命名

公开 Interface 使用 `LogService_*`；私有类型、静态对象和局部变量使用 `snake_case`。
