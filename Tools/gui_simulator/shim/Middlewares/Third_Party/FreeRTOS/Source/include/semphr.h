/**
  ******************************************************************************
  * @file    semphr.h
  * @brief   模拟器 FreeRTOS 互斥锁替身：单线程，拿锁总成功。
  *
  * @details
  *          模拟器只有 GUI 主循环一个线程（假前台、假播放与 GUI step 在同一个线程里顺序执行），所以 `ui_cell` 的互斥锁在这里
  *          只要“拿得到、放得掉”：拿锁总成功，不记录持有状态。GUI 拿不到锁的路径（下一帧再试）归 `Tests/ui_cell`，不在模拟器里。
  ******************************************************************************
  */

#ifndef SIM_FREERTOS_SEMPHR_H
#define SIM_FREERTOS_SEMPHR_H

#include "FreeRTOS.h"

/** @brief 静态互斥量的存储；替身不用里面的内容，只占位。 */
typedef struct
{
    int unused;
} StaticSemaphore_t;

typedef StaticSemaphore_t *SemaphoreHandle_t;

/**
 * @brief 静态创建互斥量：返回传入的存储作为句柄。
 * @param[in,out] buffer 存储。
 * @return 句柄（等于 buffer）。
 */
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer)
{
    return buffer;
}

/**
 * @brief 拿锁：单线程下总成功。
 * @param[in] handle 互斥量。
 * @param[in] ticks_to_wait 未使用。
 * @return 总是 pdTRUE。
 */
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks_to_wait)
{
    (void)handle;
    (void)ticks_to_wait;
    return pdTRUE;
}

/**
 * @brief 放锁：单线程下总成功。
 * @param[in] handle 互斥量。
 * @return 总是 pdTRUE。
 */
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t handle)
{
    (void)handle;
    return pdTRUE;
}

#endif /* SIM_FREERTOS_SEMPHR_H */
