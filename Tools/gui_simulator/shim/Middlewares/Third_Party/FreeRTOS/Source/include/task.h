/**
  ******************************************************************************
  * @file    task.h
  * @brief   模拟器 FreeRTOS 任务替身：Tick 与任务通知。
  ******************************************************************************
  */

#ifndef SIM_FREERTOS_TASK_H
#define SIM_FREERTOS_TASK_H

#include "FreeRTOS.h"

typedef void *TaskHandle_t;

TickType_t xTaskGetTickCount(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskNotifyGiveIndexedFromISR(TaskHandle_t task,
                                   UBaseType_t index,
                                   BaseType_t *higher_priority_task_woken);
uint32_t ulTaskNotifyTakeIndexed(UBaseType_t index,
                                 BaseType_t clear_on_exit,
                                 TickType_t ticks_to_wait);

#endif /* SIM_FREERTOS_TASK_H */
