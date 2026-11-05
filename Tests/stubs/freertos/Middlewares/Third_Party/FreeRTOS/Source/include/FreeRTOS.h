/**
 * @file FreeRTOS.h
 * @brief 主机共用 FreeRTOS 桩：类型、常量与时间基准，不模拟真实调度器。
 */
#ifndef HOST_STUB_FREERTOS_H
#define HOST_STUB_FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;

#define configTICK_RATE_HZ 1000U
/* 与产品 FreeRTOSConfig.h 一致：每个任务 4 个通知槽。 */
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 4

#define pdFALSE ((BaseType_t)0)
#define pdTRUE ((BaseType_t)1)
#define pdPASS (pdTRUE)
#define pdFAIL (pdFALSE)

/* 与产品 FreeRTOSConfig.h 一致：configASSERT 无条件生效，失败时调用 vAssertCalled。
 * 桩里的 vAssertCalled 打印位置后 abort()；测试用 SIGABRT 处理函数捕获（见 freertos_stub.c）。 */
void vAssertCalled(const char *file, uint32_t line);
#define configASSERT(x)                              do                                               {                                                    if ((x) == 0)                                    {                                                    vAssertCalled(__FILE__, (uint32_t)__LINE__);         }                                            } while (0)

#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#define pdMS_TO_TICKS(ms) ((TickType_t)(((uint64_t)(ms) * (uint64_t)configTICK_RATE_HZ) / 1000U))
/* 主机桩没有中断，不会真的让出；记录“非零标志的让出请求”次数，供测试核对回调把 GiveFromISR 的
 * higher_priority_task_woken 交给了 portYIELD_FROM_ISR。定义在 freertos_stub.c。 */
void vStubPortYieldFromISR(BaseType_t higher_priority_task_woken);
#define portYIELD_FROM_ISR(x) vStubPortYieldFromISR((BaseType_t)(x))

#endif
