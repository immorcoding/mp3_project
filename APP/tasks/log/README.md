# Log Task

Log task 是 `LogService` 的唯一消费者。它以短周期推进 `Components/log` 的输出端，并将 ready queue 中的一条消息转交给日志核心。

## 公开 Interface

- `log_task(void *handle)`：仅由 `APP/tasks/app_tasks.c` 创建。

## 调用的 Interface

- `LogService_Consume()`；
- FreeRTOS 的延时 Interface。

## 资源与约束

- 不拥有消息块；`LogService` 负责 `free → ready → free` 的所有权转移；
- 不从 ISR 调用；
- 不直接访问 USB CDC 或 `LOG_OutputOpsTypeDef` 的具体 Implementation。

## 命名

Task 入口使用 `log_task()`；本目录私有符号使用 `snake_case`。
