# 共用 FreeRTOS 主机桩

所有主机测试工程共用的 FreeRTOS 替身。头文件按产品的 include 路径摆放（`Middlewares/Third_Party/FreeRTOS/Source/include/`），测试工程把本目录放在 `../..` 之前即可让 `#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"` 取到桩。

## 提供什么

- `FreeRTOS.h`：`TickType_t`、`BaseType_t`、`pd*` 常量、`portMAX_DELAY`、`pdMS_TO_TICKS`。
- `semphr.h`：互斥锁与二值信号量（动态与静态创建、`Take`、`Give`、`GiveFromISR`、`Delete`）。
- `task.h`：`xTaskGetTickCount`、`xTaskGetCurrentTaskHandle`，以及带下标的任务通知（`xTaskNotifyIndexed`、`xTaskNotifyWaitIndexed`、`ulTaskNotifyTakeIndexed`、`xTaskNotifyStateClearIndexed` 与 `FromISR` 版本；不带下标的写法映射到 0 号槽）。
- `freertos_stub.h`：脚本化与观察接口。`freertos_stub_reset` 清空一切；`freertos_stub_set_tick` / `set_tick_step` 控制时间；`freertos_stub_semaphore_fail_next_takes` 让接下来 N 次拿取失败（“拿不到锁”）；`freertos_stub_set_block_hook` 安装阻塞钩子，在“将要等待”时替别的任务或中断做事（“关闸时等待”：钩子里放行或不放行）；`freertos_stub_semaphore_stats`、`freertos_stub_notification_*`、`freertos_stub_notify_calls` 供测试读取结果。

## 保真边界

主机测试是单线程的，桩不切换任务也不真的阻塞：拿不到时先调一次钩子，再检查一次，仍不可用就让 tick 前进超时长度并返回失败；`portMAX_DELAY` 的等待若无人放行也返回失败，并计入 `freertos_stub_unsatisfied_infinite_waits`（真实环境里这些调用会一直阻塞）。

PASS 证明被测代码在“拿到、拿不到、等到、等不到”各分支上行为正确；不证明真实并发、优先级继承、递归持有、中断时序，也没有实现队列、计数信号量和定时器。这些留给板级验收。

桩自身的行为测试在 `Tests/flash_ftl/test_freertos_stub.c`（CTest `freertos_stub_behavior`）。
