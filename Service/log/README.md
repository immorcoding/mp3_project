# Service_Log

Service_Log 将多个普通任务的短日志文本汇集到静态消息块池，并由 Log task 顺序转交给 `Components/log`。它解决多任务生产与单一输出消费者之间的同步，不取代日志核心的格式化和 USB 输出能力。

## 公开 Interface

- `Service_Log_Init()`：创建 free queue、ready queue 并归入全部静态消息块；
- `Service_Log_Post()`：非阻塞投递一条已格式化的文本；
- `Service_Log_Consume()`：由 Log task 推进输出，并至多转交一个 ready 消息；
- `Service_Log_GetDropStats()`：读日志丢失累计（DIAG-6、#204），任意普通任务可调；供 Monitor 的 `Health` 行。`ServiceDropped` 是 `Service_Log_Post()` 没能交给 Log task 的条数（消息池满返回 `SERVICE_BUSY`、队列转移失败、文本过长），多个生产者任务在临界区内自增；`CoreDropped` 是 `LOG_StatsTypeDef.DroppedCount`（核心队列溢出或发送错误）的副本，由 Log task 在每次 `Service_Log_Consume()` 里刷新，所以调用者不碰只许 Log task 访问的日志核心（DIAG-1）。参数无效（空指针、非法等级）与尚未初始化不计入丢失。两项各自单调不降，32 位回绕时用无符号差值。

所有公开函数返回 `Service_StatusTypeDef`；`Service_Log_Post()` 的日志等级使用
`Service_Log_LevelTypeDef`，不向调用者泄漏 `Components/log` 的类型。

## 编译期依赖

- `LOG_Process()`、`LOG_Printf()`、`LOG_GetStats()`；
- FreeRTOS queue Interface。

## 运行时请求与事件路径

普通 Task 经 `Service_Log_Post()` 投递，Log task 经 `Service_Log_Consume()` 向下调用日志核心。当前没有硬件 ISR 直接进入本 Module；ISR 如需日志只能先通知普通任务。

## 资源与约束

- 队列深度宏在 `log_service_config.h`，由 `log_service.h` 再导出；其他 Module 不得直接包含 config。
- 所有权路径固定为 `free → producer → ready → Log task → free`；
- `tag` 使用静态存储期字符串（通常是字符串字面量），避免异步投递后调用者栈或临时对象失效；
- 仅普通任务可调用，不支持 ISR；队列满时不等待并返回错误。
- `Service_Log_Init()` 必须早于所有可能投递日志的任务。
- **消息池深度与节拍（DIAG-3、#204）**：池深 `LOG_SERVICE_QUEUE_LENGTH` 为 30 块，每块正文 128 B。Log task 每 5 ms 一轮，每轮至多把一个 ready 消息转交日志核心（核心队列深度 5，满时暂不取），所以稳态排空速度至多约 200 行/s（USB 主机在读时）。一次突发的行数 B 约在 B × 5 ms 内排空；突发加上同一时刻其他任务的投递要少于 30 才不丢。当前最大的周期突发是 Monitor 每 5 s 的 12 行（约 60 ms 排空），核算见 [Monitor](../../APP/tasks/monitor/README.md)。USB 主机不读时，核心队列 5 条加池内 30 块之后继续投递就会被丢弃并计入 `ServiceDropped`。池深不变，新增汇总类日志须注明节拍并核算。

## 命名

公开 Interface 使用 `Service_Log_*`；私有类型、静态对象和局部变量使用 `snake_case`。
