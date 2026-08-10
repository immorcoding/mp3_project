# APP Tasks

本目录包含由 `app_task_start()` 创建的 FreeRTOS Task 入口和任务规格。任务创建顺序、名称、栈深度和优先级集中在 `app_tasks.c/.h`，各叶目录只维护本任务的运行逻辑。

## 公开 Interface

- `app_task_start()`：创建 bootstrap task 并启动调度器；
- `log_task()`、`storage_task()`、`monitor_task()`：仅作为 FreeRTOS `TaskFunction_t` 交给创建器。

## 调用的 Interface

- 同目录任务配置宏；
- `Service`、`Platform` 的公开 Interface；
- FreeRTOS Task、通知和诊断 Interface。

## 约束

- 任务入口不得返回；
- Task 之间不能共享 FatFs 对象或绕过其资源所有者；
- ISR 只唤醒任务，不能把文件系统、日志格式化或设备初始化移入中断。

## 命名

任务入口和内部 Implementation 使用 `snake_case`；栈/优先级宏以 `APP_<TASK>_...` 命名。
