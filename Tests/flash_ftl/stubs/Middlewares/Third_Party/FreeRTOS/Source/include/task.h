/**
 * @file task.h
 * @brief 主机测试的任务与时钟查询声明；实现见各测试或 stubs/filesystem_lock_host.c。
 */
#ifndef FILE_BENCH_TEST_TASK_H
#define FILE_BENCH_TEST_TASK_H
#include <stdint.h>

typedef void *TaskHandle_t;

uint32_t xTaskGetTickCount(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
const char *pcTaskGetName(TaskHandle_t task);
#endif
