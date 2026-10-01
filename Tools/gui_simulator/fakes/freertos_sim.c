/**
  ******************************************************************************
  * @file    freertos_sim.c
  * @brief   FreeRTOS 任务替身：Tick 取模拟器时钟，通知为单线程计数器。
  ******************************************************************************
  */

#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include "sim_clock.h"

/** @brief 唯一的模拟 GUI Task 句柄；只用作非空标识。 */
static int sim_gui_task;
static uint32_t sim_notify_count[configTASK_NOTIFICATION_ARRAY_ENTRIES];

TickType_t xTaskGetTickCount(void)
{
    return (TickType_t)sim_clock_now();
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return &sim_gui_task;
}

void vTaskNotifyGiveIndexedFromISR(TaskHandle_t task,
                                   UBaseType_t index,
                                   BaseType_t *higher_priority_task_woken)
{
    (void)task;

    if (index < (UBaseType_t)configTASK_NOTIFICATION_ARRAY_ENTRIES)
    {
        sim_notify_count[index]++;
    }

    if (higher_priority_task_woken != NULL)
    {
        *higher_priority_task_woken = pdFALSE;
    }
}

/**
 * @note LCD 替身同步完成写入，调用方在等待前通知已到达；单线程下不能真正阻塞，
 *       因此无论计数是否为零都立即返回。
 */
uint32_t ulTaskNotifyTakeIndexed(UBaseType_t index,
                                 BaseType_t clear_on_exit,
                                 TickType_t ticks_to_wait)
{
    uint32_t value;

    (void)ticks_to_wait;

    if (index >= (UBaseType_t)configTASK_NOTIFICATION_ARRAY_ENTRIES)
    {
        return 0U;
    }

    value = sim_notify_count[index];
    if (value != 0U)
    {
        sim_notify_count[index] = (clear_on_exit != pdFALSE) ? 0U : (value - 1U);
    }

    return value;
}
