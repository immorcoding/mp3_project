/**
  ******************************************************************************
  * @file    monitor_task.c
  * @brief   FreeRTOS 任务栈高水位线与诊断 LED 监控任务。
  *
  * @details
  *          HighWaterMark 是任务自创建以来的最小剩余栈，因此计算出的占用率是
  *          历史峰值，不是函数调用瞬间的实时栈指针。任务快照由 FreeRTOS 堆临时
  *          分配；完成输出后必须在同一任务上下文归还。
  ******************************************************************************
  */

#include "APP/tasks/monitor/monitor_task.h"
#include "APP/tasks/monitor/monitor_task_config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "APP/tasks/app_tasks.h"
#include "Components/log/log.h"
#include "Platform/led/platform_led.h"
#include "Platform/temp/platform_temp.h"
#include "Service/log/log_service.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/portable.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/**
  * @brief  返回由 App 创建或 FreeRTOS 内核创建的任务总栈深度。
  * @param  task_name uxTaskGetSystemState() 返回的稳定任务名称。
  * @retval 栈深度，单位为 StackType_t；未登记任务返回 0。
  */
static UBaseType_t monitor_get_task_stack_words(const char *task_name)
{
    if (strcmp(task_name, "Create Task") == 0)
    {
        return APP_BOOT_TASK_STACK_WORDS;
    }

    if (strcmp(task_name, "Storage Task") == 0)
    {
        return APP_STORAGE_TASK_STACK_WORDS;
    }

    if (strcmp(task_name, "Log Task") == 0)
    {
        return APP_LOG_TASK_STACK_WORDS;
    }

    if (strcmp(task_name, "Monitor Task") == 0)
    {
        return APP_MONITOR_TASK_STACK_WORDS;
    }

    // if (strcmp(task_name, "LCD Task") == 0)
    // {
    //     return APP_LCD_TASK_STACK_WORDS;
    // }

    if (strcmp(task_name, "IDLE") == 0)
    {
        return configMINIMAL_STACK_SIZE;
    }

#if (configUSE_TIMERS == 1)
    if (strcmp(task_name, "Tmr Svc") == 0)
    {
        return configTIMER_TASK_STACK_DEPTH;
    }
#endif

    return 0U;
}

/**
  * @brief  取得当前任务快照并记录各任务的栈历史峰值。
  * @note   FreeRTOS 仅保证 HighWaterMark 的单位为 StackType_t。输出字节数时
  *         必须乘 sizeof(StackType_t)；未知任务的总栈深度为 0，不能计算比例。
  */
static void monitor_log_stack_snapshot(void)
{
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_snapshot;

    task_snapshot = pvPortMalloc(task_count * sizeof(TaskStatus_t));
    if (task_snapshot == NULL)
    {
        return;
    }

    task_count = uxTaskGetSystemState(task_snapshot, task_count, NULL);

    for (UBaseType_t index = 0U; index < task_count; index++)
    {
        char message[MONITOR_LOG_MESSAGE_LENGTH];
        UBaseType_t total_words = monitor_get_task_stack_words(task_snapshot[index].pcTaskName);
        UBaseType_t free_words = task_snapshot[index].usStackHighWaterMark;
        UBaseType_t used_words;
        unsigned long peak_x10;

        if ((total_words == 0U) || (free_words > total_words))
        {
            continue;
        }

        used_words = total_words - free_words;
        peak_x10 = ((unsigned long)used_words * 1000UL) / (unsigned long)total_words;

        (void)snprintf(message,
                       sizeof(message),
                       "%s: total=%lu B, min free=%lu B, peak=%lu.%lu%%",
                       task_snapshot[index].pcTaskName,
                       (unsigned long)(total_words * sizeof(StackType_t)),
                       (unsigned long)(free_words * sizeof(StackType_t)),
                       peak_x10 / 10UL,
                       peak_x10 % 10UL);

        (void)Service_Log_Post(LOG_LEVEL_INFO, "MONITOR", message);
    }

    vPortFree(task_snapshot);
}

/**
  * @brief  读取并记录当前 MCU 结温。
  * @note   温度仅随既有快照周期采集，避免低价值的高频 ADC 采样和日志投递。
  *         连续失败时仅记录第一条错误；一次成功读取会解除该抑制，以便下次新故障
  *         仍能被观察到。
  */
static void monitor_log_temperature(void)
{
    static bool monitor_temperature_error_reported;
    int32_t temperature_mC;
    uint32_t magnitude_mC;
    const char *sign;
    char message[MONITOR_LOG_MESSAGE_LENGTH];

    if (Platform_Temp_Read(&temperature_mC) != PLATFORM_OK)
    {
        if (!monitor_temperature_error_reported)
        {
            (void)Service_Log_Post(LOG_LEVEL_ERROR,
                                  "MONITOR",
                                  "MCU temperature unavailable.");
            monitor_temperature_error_reported = true;
        }

        return;
    }

    monitor_temperature_error_reported = false;
    sign = (temperature_mC < 0) ? "-" : "";
    magnitude_mC = (temperature_mC < 0) ?
                       (uint32_t)(-(temperature_mC + 1)) + 1U :
                       (uint32_t)temperature_mC;

    (void)snprintf(message,
                   sizeof(message),
                   "MCU temperature: %s%lu.%03lu C.",
                   sign,
                   (unsigned long)(magnitude_mC / 1000U),
                   (unsigned long)(magnitude_mC % 1000U));
    (void)Service_Log_Post(LOG_LEVEL_INFO, "MONITOR", message);
}

/**
  * @brief  周期性记录任务栈历史峰值并翻转诊断 LED。
  * @param  handle 未使用，保留以满足 FreeRTOS TaskFunction_t。
  * @note   监控周期为 500 ms；每 10 个周期采集一次快照，避免监控日志本身持续
  *         占用 Service_Log 队列和其他任务的 CPU 时间。
  */
void monitor_task(void *handle)
{
    uint16_t period_count = 0U;

    (void)handle;

    /* 温度采样属于诊断能力，校准失败不应阻止整机和其他任务启动。 */
    (void)Platform_Temp_Init();

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(MONITOR_TASK_PERIOD_MS));

        if ((period_count++ % MONITOR_SNAPSHOT_INTERVAL) == 0U)
        {
            monitor_log_stack_snapshot();
            monitor_log_temperature();
        }

        (void)Platform_LED_Toggle(PLATFORM_LED_ID_STATUS);
    }
}
