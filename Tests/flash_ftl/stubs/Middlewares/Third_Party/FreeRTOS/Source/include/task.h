/**
 * @file task.h
 * @brief 主机测试的任务与时钟查询声明；实现见各测试或 stubs/filesystem_lock_host.c。
 */
#ifndef FILE_BENCH_TEST_TASK_H
#define FILE_BENCH_TEST_TASK_H
#include <stdint.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"

typedef void *TaskHandle_t;

/** @brief 任务通知动作；主机测试只用 eSetBits。 */
typedef enum
{
    eNoAction = 0,
    eSetBits
} eNotifyAction;

uint32_t xTaskGetTickCount(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
const char *pcTaskGetName(TaskHandle_t task);
BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, eNotifyAction action);
#endif
