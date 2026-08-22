# Log Component

Log Component 提供等级过滤、格式化、固定 RAM 环形队列和非阻塞输出推进。它不认识 USB CDC、FreeRTOS queue 或任何具体时间源。

## 公开 Interface

- `LOG_Init()`、`LOG_Write()`、`LOG_Printf()`、`LOG_Process()`；
- `LOG_SetLevel()`、`LOG_GetStats()` 等查询 Interface；
- 输出 Ops 与时间源绑定类型。

## 编译期依赖

- 自身拥有的输出 Ops 与时间源回调类型。

## 运行时请求与事件路径

日志核心经输出 Ops 推进已绑定后端；Platform 负责绑定，Service_Log/Log task 决定 RTOS 投递与消费时序。USB 完成状态由 Adapter 消化为可重试输出结果，不直接调用日志核心。

## 约束

- 当前核心不提供多任务或 ISR 并发保护；普通任务应通过 `Service_Log_Post()` 投递；
- 不持有 USB 异步发送缓冲区；
- `LOG_OK` 表示已入队或后端已接收，不代表主机已经显示。
