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
#include "APP/tasks/gui/gui_task.h"
#include "APP/tasks/monitor/monitor_task_config.h"
#include "APP/tasks/monitor/monitor_stats.h"
#include "APP/tasks/monitor/monitor_warn.h"
#include "APP/tasks/playback/playback_task.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "APP/tasks/app_tasks.h"
#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Platform/led/platform_led.h"
#include "Platform/temp/platform_temp.h"
#include "Service/log/log_service.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/portable.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/* 一批最多的日志行数：每个任务一行栈占用，加 Heap、温度、GUI 最长帧、CPU 占比、Health 五行（DIAG-6）。
 * 一批在 5 s 内一次投完，不分散；它自己必须放得进 Service Log 的 30 块消息池，余量留给同时到来的其他任务日志。
 * 实际任务数 7，一批 12 行。 */
#define MONITOR_BATCH_FIXED_LINES 5U
_Static_assert((MONITOR_CPU_TASK_MAX + MONITOR_BATCH_FIXED_LINES) <= LOG_SERVICE_QUEUE_LENGTH,
               "one Monitor batch must fit the Service Log message pool");

/* 已告警的未登记任务名；静态零初始化即为空记录，仅 Monitor Task 访问。 */
static monitor_warn_record_t monitor_warned_tasks;

/* 5 s 批次的 CPU 占比与健康行状态；都只由 Monitor Task 访问，放静态区，不占 1 KiB 的任务栈。
 * 零初始化的基准即“首个窗口从启动算起”。 */
static monitor_cpu_baseline_t monitor_cpu_baseline;
static monitor_cpu_sample_t monitor_cpu_samples[MONITOR_CPU_TASK_MAX];
static monitor_health_t monitor_health_previous;
static char monitor_cpu_names[MONITOR_CPU_TASK_MAX][MONITOR_WARNED_NAME_LENGTH];
static uint8_t monitor_cpu_sample_count;
static uint32_t monitor_cpu_sample_total;
static TickType_t monitor_cpu_sample_tick;
static TickType_t monitor_window_start_tick;
static char monitor_line[MONITOR_LOG_LINE_LENGTH];

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

    if (strcmp(task_name, "GUI Task") == 0)
    {
        return APP_GUI_TASK_STACK_WORDS;
    }

    if (strcmp(task_name, "Playback Task") == 0)
    {
        return APP_PLAYBACK_TASK_STACK_WORDS;
    }

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
  * @brief  把任务快照里的运行计数拷进静态样本区，供稍后 monitor_log_cpu_share() 使用。
  * @param[in]  task_snapshot uxTaskGetSystemState() 填充的快照。
  * @param[in]  task_count 快照里的任务数。
  * @param[in]  run_time_total 同一次调用给出的总运行计数。
  * @note   名字拷进静态缓冲，快照释放后样本仍有效。拷贝与成行分开，使成行（含 monitor_cpu_format 的排序与
  *         snprintf）不叠在 monitor_log_stack_snapshot 的栈帧里（Monitor 栈只有 1 KiB）。
  */
static void monitor_capture_cpu_samples(const TaskStatus_t *task_snapshot,
                                        UBaseType_t task_count,
                                        uint32_t run_time_total)
{
    monitor_cpu_sample_count = 0U;
    for (UBaseType_t index = 0U; (index < task_count) && (monitor_cpu_sample_count < MONITOR_CPU_TASK_MAX); index++)
    {
        char *name = monitor_cpu_names[monitor_cpu_sample_count];

        (void)strncpy(name, task_snapshot[index].pcTaskName, MONITOR_WARNED_NAME_LENGTH - 1U);
        name[MONITOR_WARNED_NAME_LENGTH - 1U] = '\0';
        monitor_cpu_samples[monitor_cpu_sample_count].name = name;
        monitor_cpu_samples[monitor_cpu_sample_count].run_time = task_snapshot[index].ulRunTimeCounter;
        monitor_cpu_sample_count++;
    }

    monitor_cpu_sample_total = run_time_total;
    monitor_cpu_sample_tick = xTaskGetTickCount();
}

/**
  * @brief  记录 CPU 占比一行：与上一批样本的运行计数差值，单位 %，包含空闲任务（DIAG-6、R1.1）。
  * @note   用 monitor_capture_cpu_samples() 留下的静态样本；窗口长度取两批之间的 FreeRTOS 节拍差，只用于显示，
  *         首批从启动算起。格式与键的规则见 monitor_stats.h。须在 monitor_log_stack_snapshot() 返回之后调用。
  */
static void monitor_log_cpu_share(void)
{
    (void)monitor_cpu_format(monitor_line,
                             sizeof(monitor_line),
                             &monitor_cpu_baseline,
                             monitor_cpu_samples,
                             monitor_cpu_sample_count,
                             monitor_cpu_sample_total,
                             (uint32_t)((monitor_cpu_sample_tick - monitor_window_start_tick) * portTICK_PERIOD_MS));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "MONITOR", monitor_line);

    monitor_cpu_baseline_store(&monitor_cpu_baseline,
                               monitor_cpu_samples,
                               monitor_cpu_sample_count,
                               monitor_cpu_sample_total);
    monitor_window_start_tick = monitor_cpu_sample_tick;
}

/**
  * @brief  记录欠载与日志丢失一行：累计值与本批增量（DIAG-6、R1.3）。
  * @note   日志丢失取 Service_Log 的两项计数；欠载取播放任务状态机的累计。
  */
static void monitor_log_health(void)
{
    Service_Log_DropStatsTypeDef drops = {0U, 0U};
    monitor_health_t current;

    (void)Service_Log_GetDropStats(&drops);
    current.underruns = playback_task_get_underrun_total();
    current.log_service_drops = drops.ServiceDropped;
    current.log_core_drops = drops.CoreDropped;

    (void)monitor_health_format(monitor_line, sizeof(monitor_line), &current, &monitor_health_previous);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "MONITOR", monitor_line);

    monitor_health_previous = current;
}

/**
  * @brief  取得当前任务快照并记录各任务的栈历史峰值。
  * @note   FreeRTOS 仅保证 HighWaterMark 的单位为 StackType_t。输出字节数时
  *         必须乘 sizeof(StackType_t)；未知任务的总栈深度为 0，不能计算比例，
  *         改为每个任务名投递一条 WARN 日志（记录写满后不再告警）。
  */
static void monitor_log_stack_snapshot(void)
{
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_snapshot;
    uint32_t run_time_total = 0U;

    task_snapshot = pvPortMalloc(task_count * sizeof(TaskStatus_t));
    if (task_snapshot == NULL)
    {
        return;
    }

    task_count = uxTaskGetSystemState(task_snapshot, task_count, &run_time_total);

    for (UBaseType_t index = 0U; index < task_count; index++)
    {
        char message[MONITOR_LOG_MESSAGE_LENGTH];
        UBaseType_t total_words = monitor_get_task_stack_words(task_snapshot[index].pcTaskName);
        UBaseType_t free_words = task_snapshot[index].usStackHighWaterMark;
        UBaseType_t used_words;
        unsigned long peak_x10;

        if (total_words == 0U)
        {
            /* 未登记任务算不出占用率；每个任务名只告警一次，让漏登记在日志里可见。 */
            if (monitor_warn_should_report(&monitor_warned_tasks, task_snapshot[index].pcTaskName))
            {
                (void)snprintf(message,
                               sizeof(message),
                               "Task not registered in monitor: %s",
                               task_snapshot[index].pcTaskName);
                (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, "MONITOR", message);
            }

            continue;
        }

        if (free_words > total_words)
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

        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "MONITOR", message);
    }

    monitor_capture_cpu_samples(task_snapshot, task_count, run_time_total);

    vPortFree(task_snapshot);

    /* 堆历史最小余量含上面临时快照的占用，比业务实际占用略保守。 */
    {
        char heap_message[MONITOR_LOG_MESSAGE_LENGTH];

        (void)snprintf(heap_message,
                       sizeof(heap_message),
                       "Heap: free=%lu B, min free=%lu B",
                       (unsigned long)xPortGetFreeHeapSize(),
                       (unsigned long)xPortGetMinimumEverFreeHeapSize());
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "MONITOR", heap_message);
    }
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
            (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
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
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "MONITOR", message);
}

/**
  * @brief  记录 GUI 启动以来的最长一帧耗时。
  * @note   周期数来自 gui_task_get_max_frame_cycles()；换算为微秒（保留一位小数）
  *         使用当前核心频率。尚无样本或频率不可用时不输出。
  */
static void monitor_log_gui_max_frame(void)
{
    const uint32_t cycles = gui_task_get_max_frame_cycles();
    const uint32_t frequency_hz = CortexM7CycleCounter_GetFrequencyHz();
    uint64_t us_x10;
    char message[MONITOR_LOG_MESSAGE_LENGTH];

    if ((cycles == 0U) || (frequency_hz == 0U))
    {
        return;
    }

    us_x10 = ((uint64_t)cycles * 10000000ULL) / frequency_hz;
    (void)snprintf(message,
                   sizeof(message),
                   "GUI max frame: %lu cycles = %lu.%lu us @ %lu MHz",
                   (unsigned long)cycles,
                   (unsigned long)(us_x10 / 10ULL),
                   (unsigned long)(us_x10 % 10ULL),
                   (unsigned long)(frequency_hz / 1000000UL));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "MONITOR", message);
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
            monitor_log_gui_max_frame();
            monitor_log_cpu_share();
            monitor_log_health();
        }

        (void)Platform_LED_Toggle(PLATFORM_LED_ID_STATUS);
    }
}
