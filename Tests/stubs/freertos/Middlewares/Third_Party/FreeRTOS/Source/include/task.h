/**
 * @file task.h
 * @brief 主机共用 FreeRTOS 桩：tick 查询与任务通知，行为由 freertos_stub.h 脚本化。
 */
#ifndef HOST_STUB_TASK_H
#define HOST_STUB_TASK_H

#include "FreeRTOS.h"

typedef struct HostStubTask *TaskHandle_t;

typedef enum
{
    eNoAction = 0,
    eSetBits,
    eIncrement,
    eSetValueWithOverwrite,
    eSetValueWithoutOverwrite
} eNotifyAction;

TickType_t xTaskGetTickCount(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);

BaseType_t xTaskNotifyIndexed(TaskHandle_t task, UBaseType_t index, uint32_t value, eNotifyAction action);
BaseType_t xTaskNotifyIndexedFromISR(TaskHandle_t task,
                                     UBaseType_t index,
                                     uint32_t value,
                                     eNotifyAction action,
                                     BaseType_t *higher_priority_task_woken);
BaseType_t xTaskNotifyWaitIndexed(UBaseType_t index,
                                  uint32_t clear_on_entry,
                                  uint32_t clear_on_exit,
                                  uint32_t *value,
                                  TickType_t ticks_to_wait);
BaseType_t xTaskNotifyStateClearIndexed(TaskHandle_t task, UBaseType_t index);
void vTaskNotifyGiveIndexedFromISR(TaskHandle_t task, UBaseType_t index, BaseType_t *higher_priority_task_woken);
uint32_t ulTaskNotifyTakeIndexed(UBaseType_t index, BaseType_t clear_count_on_exit, TickType_t ticks_to_wait);

/* 与真实 task.h 一致：不带下标的形式固定使用 0 号槽。 */
#define xTaskNotify(task, value, action) xTaskNotifyIndexed((task), 0U, (value), (action))
#define xTaskNotifyWait(clear_entry, clear_exit, value, ticks) \
    xTaskNotifyWaitIndexed(0U, (clear_entry), (clear_exit), (value), (ticks))
#define xTaskNotifyGiveIndexed(task, index) xTaskNotifyIndexed((task), (index), 0U, eIncrement)
#define xTaskNotifyGive(task) xTaskNotifyGiveIndexed((task), 0U)
#define vTaskNotifyGiveFromISR(task, woken) vTaskNotifyGiveIndexedFromISR((task), 0U, (woken))
#define ulTaskNotifyTake(clear, ticks) ulTaskNotifyTakeIndexed(0U, (clear), (ticks))

#endif
