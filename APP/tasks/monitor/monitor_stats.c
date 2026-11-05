/**
  ******************************************************************************
  * @file    monitor_stats.c
  * @brief   Monitor Task 5 s 批次的计算与成行实现。
  ******************************************************************************
  */

#include "APP/tasks/monitor/monitor_stats.h"

#include <stdio.h>
#include <string.h>

/** 日志里一个任务的短键长度（不含 NUL）。 */
#define MONITOR_STATS_KEY_LENGTH 4U

/** 排序用的一项：短键与占比（单位 0.1 %）。 */
typedef struct
{
    char key[MONITOR_STATS_KEY_LENGTH + 1U];
    uint32_t share_x10;
} monitor_cpu_entry_t;

/**
  * @brief  由任务名生成日志短键：首词小写后的前 MONITOR_STATS_KEY_LENGTH 个字符。
  * @param[out] key 输出，至少 MONITOR_STATS_KEY_LENGTH + 1 字节。
  * @param[in]  name 任务名。
  */
static void monitor_stats_make_key(char *key, const char *name)
{
    size_t length = 0U;

    while ((length < MONITOR_STATS_KEY_LENGTH) && (name[length] != '\0') && (name[length] != ' '))
    {
        char c = name[length];

        key[length] = ((c >= 'A') && (c <= 'Z')) ? (char)(c - 'A' + 'a') : c;
        length++;
    }

    key[length] = '\0';
}

/**
  * @brief  在基准里按任务名找上一次的累计运行计数。
  * @param[in]  baseline 基准。
  * @param[in]  name 任务名。
  * @return 上一次的计数；没有记录（新任务）时为 0。
  */
static uint32_t monitor_stats_previous_run_time(const monitor_cpu_baseline_t *baseline, const char *name)
{
    for (uint8_t index = 0U; index < baseline->count; index++)
    {
        if (strncmp(baseline->names[index], name, MONITOR_WARNED_NAME_LENGTH - 1U) == 0)
        {
            return baseline->run_time[index];
        }
    }

    return 0U;
}

/**
  * @brief  把缓冲末尾改成截断标记，使被截断的输出可见。
  * @param[out] buffer 输出缓冲。
  * @param[in]  size 缓冲长度（大于 1）。
  * @return 写入的字符数（不含 NUL）。
  */
static size_t monitor_stats_mark_truncated(char *buffer, size_t size)
{
    buffer[size - 2U] = '~';
    buffer[size - 1U] = '\0';
    return size - 1U;
}

/**
  * @brief  由两次快照的差值格式化 CPU 占比一行（格式见 monitor_stats.h）。
  * @param[out] buffer 输出缓冲，总是 NUL 结尾。
  * @param[in]  size 缓冲长度。
  * @param[in]  previous 上一次快照基准。
  * @param[in]  samples 本次快照的任务样本。
  * @param[in]  count 样本个数。
  * @param[in]  total 本次快照的总计数。
  * @param[in]  window_ms 本窗口毫秒数，只用于显示。
  * @return 写入的字符数（不含 NUL）；放不下时末字符为 '~'。
  */
size_t monitor_cpu_format(char *buffer,
                          size_t size,
                          const monitor_cpu_baseline_t *previous,
                          const monitor_cpu_sample_t *samples,
                          uint8_t count,
                          uint32_t total,
                          uint32_t window_ms)
{
    monitor_cpu_entry_t entries[MONITOR_CPU_TASK_MAX];
    const uint32_t delta_total = total - previous->total;
    uint8_t entry_count = 0U;
    size_t used;
    int written;

    if ((buffer == NULL) || (size < 2U))
    {
        return 0U;
    }

    if (delta_total == 0U)
    {
        written = snprintf(buffer, size, "CPU unavailable");
        return ((written < 0) || ((size_t)written >= size)) ? monitor_stats_mark_truncated(buffer, size)
                                                            : (size_t)written;
    }

    for (uint8_t index = 0U; (index < count) && (entry_count < MONITOR_CPU_TASK_MAX); index++)
    {
        const uint32_t delta = samples[index].run_time - monitor_stats_previous_run_time(previous, samples[index].name);
        monitor_cpu_entry_t entry;
        uint8_t slot = entry_count;

        monitor_stats_make_key(entry.key, samples[index].name);
        entry.share_x10 = (uint32_t)(((uint64_t)delta * 1000ULL) / delta_total);

        /* 插入排序：按键的字典序，每批列序一致。 */
        while ((slot > 0U) && (strcmp(entries[slot - 1U].key, entry.key) > 0))
        {
            entries[slot] = entries[slot - 1U];
            slot--;
        }
        entries[slot] = entry;
        entry_count++;
    }

    written = snprintf(buffer, size, "CPU win=%lums", (unsigned long)window_ms);
    if ((written < 0) || ((size_t)written >= size))
    {
        return monitor_stats_mark_truncated(buffer, size);
    }
    used = (size_t)written;

    for (uint8_t index = 0U; index < entry_count; index++)
    {
        written = snprintf(&buffer[used],
                           size - used,
                           " %s=%lu.%lu",
                           entries[index].key,
                           (unsigned long)(entries[index].share_x10 / 10U),
                           (unsigned long)(entries[index].share_x10 % 10U));
        if ((written < 0) || ((size_t)written >= (size - used)))
        {
            return monitor_stats_mark_truncated(buffer, size);
        }
        used += (size_t)written;
    }

    return used;
}

/**
  * @brief  把本次快照存为下一窗口的基准。
  * @param[out] baseline 待写入的基准。
  * @param[in]  samples 本次快照的任务样本。
  * @param[in]  count 样本个数；超过 MONITOR_CPU_TASK_MAX 的部分不记录。
  * @param[in]  total 本次快照的总计数。
  */
void monitor_cpu_baseline_store(monitor_cpu_baseline_t *baseline,
                                const monitor_cpu_sample_t *samples,
                                uint8_t count,
                                uint32_t total)
{
    if (baseline == NULL)
    {
        return;
    }

    (void)memset(baseline, 0, sizeof(*baseline));
    for (uint8_t index = 0U; (index < count) && (baseline->count < MONITOR_CPU_TASK_MAX); index++)
    {
        (void)strncpy(baseline->names[baseline->count], samples[index].name, MONITOR_WARNED_NAME_LENGTH - 1U);
        baseline->run_time[baseline->count] = samples[index].run_time;
        baseline->count++;
    }
    baseline->total = total;
}

/**
  * @brief  格式化欠载与日志丢失一行：累计值，括号内为本窗口增量。
  * @param[out] buffer 输出缓冲，总是 NUL 结尾。
  * @param[in]  size 缓冲长度。
  * @param[in]  current 本次累计值。
  * @param[in]  previous 上一批累计值。
  * @return 写入的字符数（不含 NUL）；放不下时末字符为 '~'。
  */
size_t monitor_health_format(char *buffer,
                             size_t size,
                             const monitor_health_t *current,
                             const monitor_health_t *previous)
{
    int written;

    if ((buffer == NULL) || (size < 2U))
    {
        return 0U;
    }

    written = snprintf(buffer,
                       size,
                       "Health underrun=%lu(+%lu) log_svc=%lu(+%lu) log_core=%lu(+%lu)",
                       (unsigned long)current->underruns,
                       (unsigned long)(current->underruns - previous->underruns),
                       (unsigned long)current->log_service_drops,
                       (unsigned long)(current->log_service_drops - previous->log_service_drops),
                       (unsigned long)current->log_core_drops,
                       (unsigned long)(current->log_core_drops - previous->log_core_drops));
    if ((written < 0) || ((size_t)written >= size))
    {
        return monitor_stats_mark_truncated(buffer, size);
    }

    return (size_t)written;
}
