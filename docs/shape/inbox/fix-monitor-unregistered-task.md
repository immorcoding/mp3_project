# fix/monitor-unregistered-task inbox

- 2026-10-19 · cite · HAR-5 · 新增主机模块 `monitor_warn`（`APP/tasks/monitor/monitor_warn.c`，未登记任务告警去重记录）。保真边界：PASS 只证明去重/容量/截断逻辑；`monitor_task.c` 外壳（任务名到栈深的登记表、`Service_Log_Post` 的 WARN）无主机测试，仍靠固件构建与板上 MONITOR 日志。漏登记检测是运行期 WARN，不是编译期保证（FreeRTOS 任务名是字符串，C 无法静态断言配对）。
