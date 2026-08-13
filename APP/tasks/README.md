# APP Tasks

本目录包含由 `app_task_start()` 创建的 FreeRTOS Task 入口和任务规格。任务创建顺序、名称、栈深度和优先级集中在 `app_tasks.c/.h`，各叶目录只维护本任务的运行逻辑。

## 公开 Interface

- `app_task_start()`：创建 bootstrap task 并启动调度器；
- `log_task()`、`storage_task()`、`monitor_task()`、`lcd_task()`：仅作为 FreeRTOS `TaskFunction_t` 交给创建器。

## 编译期依赖

- 同目录任务配置宏；
- `Service`、`Platform` 的公开 Interface；
- FreeRTOS Task、通知和诊断 Interface。

## 运行时请求与事件路径

Task 是 APP 创建后的运行时执行上下文：任务通过 Service/Platform 的公开 Interface 发起请求，硬件 ISR 只经已注册回调或 FromISR 原语唤醒对应任务。任务目录不拥有 HAL ISR 入口。

## 约束

- 任务入口不得返回；
- Task 之间不能共享 FatFs 对象或绕过其资源所有者；
- ISR 只唤醒任务，不能把文件系统、日志格式化或设备初始化移入中断。

## 命名

任务入口和内部 Implementation 使用 `snake_case`；栈/优先级宏以 `APP_<TASK>_...` 命名。
