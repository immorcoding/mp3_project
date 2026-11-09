# 共用 FreeRTOS 主机桩

所有主机测试工程共用的 FreeRTOS 替身。头文件按产品的 include 路径摆放（`Middlewares/Third_Party/FreeRTOS/Source/include/`），测试工程把本目录放在 `../..` 之前即可让 `#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"` 取到桩。

## 提供什么

- `FreeRTOS.h`：`TickType_t`、`BaseType_t`、`pd*` 常量、`portMAX_DELAY`、`pdMS_TO_TICKS`。
- `semphr.h`：互斥锁与二值信号量（动态与静态创建、`Take`、`Give`、`GiveFromISR`、`Delete`）。
- `event_groups.h`：静态事件组（`xEventGroupCreateStatic`、`SetBits`、`ClearBits`、`GetBits`、`WaitBits`；位 0 至 23）。等待的位没置上且超时不为 0 时，先调阻塞钩子（`FREERTOS_STUB_WAIT_EVENT_GROUP`，`FreertosStub_Wait.event_group` 给出句柄），仍不满足按超时返回当前位（无限等待无人置位计入未满足次数，也受死锁保护）。`freertos_stub_event_group_stats` 读等待、阻塞、置位、清位次数，`freertos_stub_last_created_event_group` 取模块内部创建的事件组。没有等待者列表：开闸时“多个等待者一起醒”靠钩子里置位脚本化，不模拟谁先醒。
- `task.h`：`xTaskGetTickCount`、`vTaskDelay`（只让 tick 前进）、`xTaskGetCurrentTaskHandle`，以及带下标的任务通知（`xTaskNotifyIndexed`、`xTaskNotifyWaitIndexed`、`ulTaskNotifyTakeIndexed`、`xTaskNotifyStateClearIndexed` 与 `FromISR` 版本；不带下标的写法映射到 0 号槽）。
- `FreeRTOS.h` 还提供与产品一致的 `configASSERT`（无条件生效，失败调用桩里的 `vAssertCalled`：打印文件和行号后 `abort()`；测试用 `SIGABRT` 处理函数捕获）。
- `task.h` 里的 `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()` 是不做事的宏（主机没有中断，只让被测代码里成对嵌套的写法能编译）。
- `freertos_stub.h`：脚本化与观察接口。`freertos_stub_reset` 清空一切；`freertos_stub_set_tick` / `set_tick_step` 控制时间；`freertos_stub_semaphore_fail_next_takes` 让接下来 N 次拿取失败（“拿不到锁”）；`freertos_stub_set_block_hook` 安装阻塞钩子，在“将要等待”时替别的任务或中断做事（“关闸时等待”：钩子里放行或不放行）；`freertos_stub_last_created_semaphore` 交出最近创建的信号量（被测模块把锁藏在内部时用它取句柄）；`freertos_stub_created_semaphore_count`/`freertos_stub_created_semaphore` 按创建顺序枚举自 reset 以来创建的全部信号量（模块有多把内部锁时用，可以按“哪把被拿了”认出各自是谁；模块的锁是第一次用到时才建的，所以要在第一次调用之前 reset，之后不再 reset）；`freertos_stub_set_notify_hook` 安装通知钩子（`FreertosStub_NotifyHook`），每次 `xTaskNotifyIndexed`（含 `FromISR` 版本）在通知落进目标槽**之前**调用一次，用来看“这次通知发出时被测模块的状态”（卷事件的“先更新再叫醒”靠它证明），自测在 `freertos_stub_behavior`；`freertos_stub_semaphore_stats`（含经 `GiveFromISR` 的调用数 `give_from_isr_calls`）、`freertos_stub_notification_*`、`freertos_stub_notify_calls` 供测试读取结果；`freertos_stub_set_give_from_isr_woken` 让 `GiveFromISR` 报告“唤醒了更高优先级任务”，`freertos_stub_isr_yield_requests` 读出 `portYIELD_FROM_ISR` 以非零标志被调用的次数（桩不真的让出），用来核对 ISR 回调把标志交给了 `portYIELD_FROM_ISR`。

## 保真边界

主机测试是单线程的，桩不切换任务也不真的阻塞：拿不到时先调一次钩子，再检查一次，仍不可用就让 tick 前进超时长度并返回失败；`portMAX_DELAY` 的等待若无人放行也返回失败，并计入 `freertos_stub_unsatisfied_infinite_waits`（真实环境里这些调用会一直阻塞）。

无限等待连续 10 万次都没人放行，桩判为死锁并 abort（被测代码“等到拿到为止”的重试循环否则会空转到测试超时），所以测试必须在钩子里放行；`INCLUDE_vTaskSuspend` 为 0 时真实内核的 `portMAX_DELAY` 也只是约 49.7 天的有限超时，被测代码因此要在超时后继续等。

PASS 证明被测代码在“拿到、拿不到、等到、等不到”各分支上行为正确；不证明真实并发、优先级继承、递归持有、中断时序，也没有实现队列、计数信号量和定时器；事件组只有上面这些。这些留给板级验收。

桩自身的行为测试在 `Tests/flash_ftl/test_freertos_stub.c`（CTest `freertos_stub_behavior`）。
