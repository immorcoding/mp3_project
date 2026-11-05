/**
  ******************************************************************************
  * @file    monitor_stats.h
  * @brief   Monitor Task 5 s 批次的计算与成行（纯 C，无 RTOS 依赖）。
  *
  * @details
  *          CPU 占比由两次 FreeRTOS 运行时计数快照的差值算出，日志丢失与欠载取累计值
  *          并附本窗口增量。所有函数只做算术与格式化，不读时钟、不投递日志；调用方
  *          （monitor_task.c）负责取快照与投递。DIAG-6、#204。
  ******************************************************************************
  */

#ifndef MONITOR_STATS_H
#define MONITOR_STATS_H

#include <stddef.h>
#include <stdint.h>

#include "APP/tasks/monitor/monitor_task_config.h"

/** 一个任务在某次快照里的累计运行计数（FreeRTOS `ulRunTimeCounter`）。 */
typedef struct
{
    const char *name;     /**< FreeRTOS 任务名，调用期间有效。 */
    uint32_t run_time;    /**< 自启动累计运行计数；32 位回绕，差值用无符号减法。 */
} monitor_cpu_sample_t;

/** 上一次快照的基准；调用方持有，零初始化即“从启动算起”。 */
typedef struct
{
    char names[MONITOR_CPU_TASK_MAX][MONITOR_WARNED_NAME_LENGTH]; /**< 任务名（可能被截断）。 */
    uint32_t run_time[MONITOR_CPU_TASK_MAX];                      /**< 与 names 同下标的累计运行计数。 */
    uint8_t count;                                                /**< 已使用的记录数。 */
    uint32_t total;                                               /**< 上一次快照的总计数。 */
} monitor_cpu_baseline_t;

/** 开机以来的累计事实；每项单调不降，32 位回绕时增量仍正确。 */
typedef struct
{
    uint32_t underruns;         /**< 播放欠载次数。 */
    uint32_t log_service_drops; /**< Service Log 消息池满而丢弃的条数。 */
    uint32_t log_core_drops;    /**< Log 核心队列溢出或发送错误的丢弃数（`LOG_StatsTypeDef.DroppedCount`）。 */
} monitor_health_t;

/**
  * @brief  由两次快照的差值格式化 CPU 占比一行。
  * @param  buffer 输出缓冲，总是 NUL 结尾。
  * @param  size 缓冲长度。
  * @param  previous 上一次快照基准；全零表示从启动算起（首个窗口）。
  * @param  samples 本次快照的任务样本。
  * @param  count 样本个数。
  * @param  total 本次快照的总计数。
  * @param  window_ms 本窗口经过的毫秒数，只用于显示。
  * @retval 写入的字符数（不含 NUL）；缓冲放不下时末字符改为 '~'，截断可见而不静默。
  * @note   格式 `CPU win=<ms>ms <键>=<百分比>…`，百分比一位小数（向下取整）；键为任务名首词小写后
  *         的前四个字符（`IDLE` 为 `idle`，`Playback Task` 为 `play`），按键的字典序排列，使每批
  *         列序一致。总计数没有前进时输出 `CPU unavailable`。
  */
size_t monitor_cpu_format(char *buffer,
                          size_t size,
                          const monitor_cpu_baseline_t *previous,
                          const monitor_cpu_sample_t *samples,
                          uint8_t count,
                          uint32_t total,
                          uint32_t window_ms);

/**
  * @brief  把本次快照存为下一窗口的基准。
  * @param  baseline 待写入的基准。
  * @param  samples 本次快照的任务样本。
  * @param  count 样本个数；超过 MONITOR_CPU_TASK_MAX 的部分不记录。
  * @param  total 本次快照的总计数。
  */
void monitor_cpu_baseline_store(monitor_cpu_baseline_t *baseline,
                                const monitor_cpu_sample_t *samples,
                                uint8_t count,
                                uint32_t total);

/**
  * @brief  格式化欠载与日志丢失一行：累计值，括号内为本窗口增量。
  * @param  buffer 输出缓冲，总是 NUL 结尾。
  * @param  size 缓冲长度。
  * @param  current 本次累计值。
  * @param  previous 上一批累计值。
  * @retval 写入的字符数（不含 NUL）。
  * @note   格式 `Health underrun=<累计>(+<增量>) log_svc=<累计>(+<增量>) log_core=<累计>(+<增量>)`。
  *         给累计值而不只给增量：这一行自己被丢掉时，下一行仍能看出中间丢过。
  */
size_t monitor_health_format(char *buffer,
                             size_t size,
                             const monitor_health_t *current,
                             const monitor_health_t *previous);

#endif /* MONITOR_STATS_H */
