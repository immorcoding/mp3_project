/**
 * @file semphr.h
 * @brief 主机共用 FreeRTOS 桩：互斥锁与二值信号量，行为由 freertos_stub.h 脚本化。
 */
#ifndef HOST_STUB_SEMPHR_H
#define HOST_STUB_SEMPHR_H

#include "FreeRTOS.h"

/* 静态版本的缓冲区；桩把自己的状态放进去，大小由 freertos_stub.c 静态断言保证。 */
typedef struct
{
    void *opaque[16];
} StaticSemaphore_t;

typedef StaticSemaphore_t *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *buffer);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks_to_wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t *higher_priority_task_woken);

#endif
