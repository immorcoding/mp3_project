/**
  ******************************************************************************
  * @file    foreground_report.c
  * @brief   前台任务的日志汇总实现（spike #191）。
  ******************************************************************************
  */

#include "APP/tasks/foreground/foreground_report.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "APP/tasks/foreground/foreground_cycle.h"
#include "APP/tasks/foreground/spike_audio_read.h"
#include "APP/tasks/foreground/spike_unplug.h"
#include "APP/tasks/gui/gui_task.h"
#include "APP/tasks/playback/playback_task.h"
#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Service/filesystem/filesystem_spike.h"
#include "Service/log/log_service.h"

/** @brief 本模块发出的所有日志的标签。 */
static const char foreground_report_tag[] = "SPIKE";

/** @brief 每行日志之后让出的毫秒数；Log Task 每 5 ms 消费一条。 */
#define FOREGROUND_REPORT_PACE_MS 6U
/** @brief CPU 统计一次最多处理的任务数；任务更多时跳过这一行。 */
#define FOREGROUND_REPORT_MAX_TASKS 12U
#define FOREGROUND_REPORT_NAME_BYTES 16U

/** @brief 上一次 CPU 统计时某个任务的累计运行计数。 */
typedef struct
{
    char Name[FOREGROUND_REPORT_NAME_BYTES];
    uint32_t RunTime;
} ForegroundReportTaskBaselineTypeDef;

/** @brief CPU 汇总里列出的任务：FreeRTOS 任务名与日志里的短名。 */
typedef struct
{
    const char *TaskName;
    const char *ShortName;
} ForegroundReportCpuColumnTypeDef;

static const ForegroundReportCpuColumnTypeDef foreground_report_columns[] = {
    {"IDLE", "idle"},
    {"GUI Task", "gui"},
    {"Foreground Task", "fg"},
    {"Storage Task", "sto"},
    {"Playback Task", "pb"},
    {"Log Task", "log"},
    {"Monitor Task", "mon"},
    {"Tmr Svc", "tmr"},
};
#define FOREGROUND_REPORT_COLUMN_COUNT (sizeof(foreground_report_columns) / sizeof(foreground_report_columns[0]))

/* 任务快照放静态区，不占前台任务的栈，也不动用内核堆。 */
static TaskStatus_t foreground_report_snapshot[FOREGROUND_REPORT_MAX_TASKS];
static ForegroundReportTaskBaselineTypeDef foreground_report_previous[FOREGROUND_REPORT_MAX_TASKS];
static UBaseType_t foreground_report_previous_count;
static uint32_t foreground_report_previous_total;
static TickType_t foreground_report_previous_tick;
static GUI_TaskStatsTypeDef foreground_report_previous_gui;
static uint32_t foreground_report_previous_underruns;

/**
 * @brief 格式化一条 SPIKE 日志并非阻塞投递，随后让出几毫秒。
 * @param[in] format printf 风格格式串；结果截断到 127 字节。
 * @note  消息池满时等一会儿重试一次，仍失败就丢弃。
 */
void foreground_logf(const char *format, ...)
{
    char text[128];
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);

    if (Service_Log_Post(SERVICE_LOG_LEVEL_INFO, foreground_report_tag, text) == SERVICE_BUSY)
    {
        vTaskDelay(pdMS_TO_TICKS(2U * FOREGROUND_REPORT_PACE_MS));
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, foreground_report_tag, text);
    }
    vTaskDelay(pdMS_TO_TICKS(FOREGROUND_REPORT_PACE_MS));
}

/**
 * @brief 把任务名里的空格换成下划线，方便按列 grep。
 * @param[out] destination 至少 FOREGROUND_REPORT_NAME_BYTES 字节。
 * @param[in] source 任务名，可为空串。
 */
static void foreground_report_clean_name(char *destination, const char *source)
{
    uint32_t index;

    for (index = 0U; (index < (FOREGROUND_REPORT_NAME_BYTES - 1U)) && (source[index] != '\0'); index++)
    {
        destination[index] = (source[index] == ' ') ? '_' : source[index];
    }
    destination[index] = '\0';
    if (index == 0U)
    {
        destination[0] = '-';
        destination[1] = '\0';
    }
}

/**
 * @brief 在上一次快照里按名字找任务的累计运行计数。
 * @param[in] name 任务名。
 * @return 累计运行计数；没有记录（新任务）时为 0。
 */
static uint32_t foreground_report_previous_runtime(const char *name)
{
    UBaseType_t index;

    for (index = 0U; index < foreground_report_previous_count; index++)
    {
        if (strcmp(foreground_report_previous[index].Name, name) == 0)
        {
            return foreground_report_previous[index].RunTime;
        }
    }
    return 0U;
}

/**
 * @brief 取任务快照，返回任务数；任务过多或取不到时返回 0。
 * @param[out] total FreeRTOS 运行时统计的总计数。
 * @return 任务数。
 */
static UBaseType_t foreground_report_take_snapshot(uint32_t *total)
{
    if (uxTaskGetNumberOfTasks() > FOREGROUND_REPORT_MAX_TASKS)
    {
        return 0U;
    }
    return uxTaskGetSystemState(foreground_report_snapshot, FOREGROUND_REPORT_MAX_TASKS, total);
}

/**
 * @brief 把当前任务快照存为下一次窗口差值的基准。
 * @param[in] count 快照里的任务数。
 * @param[in] total 总计数。
 */
static void foreground_report_store_baseline(UBaseType_t count, uint32_t total)
{
    UBaseType_t index;

    for (index = 0U; index < count; index++)
    {
        strncpy(foreground_report_previous[index].Name, foreground_report_snapshot[index].pcTaskName,
                FOREGROUND_REPORT_NAME_BYTES - 1U);
        foreground_report_previous[index].Name[FOREGROUND_REPORT_NAME_BYTES - 1U] = '\0';
        foreground_report_previous[index].RunTime = foreground_report_snapshot[index].ulRunTimeCounter;
    }
    foreground_report_previous_count = count;
    foreground_report_previous_total = total;
}

/**
 * @brief 建立 CPU、GUI 与欠载的窗口基准（第一个窗口从这里开始算）。
 */
void foreground_report_baseline(void)
{
    uint32_t total = 0U;
    UBaseType_t count = foreground_report_take_snapshot(&total);

    foreground_report_store_baseline(count, total);
    foreground_report_previous_tick = xTaskGetTickCount();
    gui_task_get_stats(&foreground_report_previous_gui);
    gui_task_stats_window_reset();
    foreground_report_previous_underruns = playback_task_get_underrun_total();
}

/**
 * @brief 输出 CPU 占比一行：FreeRTOS 运行时统计的窗口差值，不是 LVGL 浮层的 CPU%。
 * @param[in] phase 阶段名。
 * @param[in] window_ms 本窗口经过的毫秒数。
 * @note  占比单位 0.1%。`cnt/ms` 是运行时计数器每毫秒的计数，用来核对 TIM5 频率：
 *        TIM5 预分频 4799，定时器时钟 240 MHz（APB1 120 MHz 乘 2）时应为 50.0，32 位约 23.9 小时回绕；
 *        差值用无符号减法，窗口远短于回绕周期。
 */
static void foreground_report_cpu(const char *phase, uint32_t window_ms)
{
    uint32_t total = 0U;
    UBaseType_t count = foreground_report_take_snapshot(&total);
    uint32_t delta_total = total - foreground_report_previous_total;
    char line[128];
    size_t used;
    uint32_t sum_x10 = 0U;
    uint32_t counts_per_ms_x10;
    size_t column;
    UBaseType_t index;

    if ((count == 0U) || (delta_total == 0U))
    {
        foreground_logf("CPU %s unavailable tasks=%lu", phase, (unsigned long)uxTaskGetNumberOfTasks());
        return;
    }

    counts_per_ms_x10 = (uint32_t)((uint64_t)delta_total * 10ULL / ((window_ms != 0U) ? window_ms : 1U));
    used = (size_t)snprintf(line, sizeof(line), "CPU %s win=%lums cnt/ms=%lu.%lu", phase, (unsigned long)window_ms,
                            (unsigned long)(counts_per_ms_x10 / 10U), (unsigned long)(counts_per_ms_x10 % 10U));
    for (column = 0U; column < FOREGROUND_REPORT_COLUMN_COUNT; column++)
    {
        uint32_t share_x10 = 0U;

        for (index = 0U; index < count; index++)
        {
            if (strcmp(foreground_report_snapshot[index].pcTaskName, foreground_report_columns[column].TaskName) == 0)
            {
                const uint32_t delta = foreground_report_snapshot[index].ulRunTimeCounter -
                                       foreground_report_previous_runtime(foreground_report_columns[column].TaskName);

                share_x10 = (uint32_t)((uint64_t)delta * 1000ULL / delta_total);
                break;
            }
        }
        sum_x10 += share_x10;
        if (used < sizeof(line))
        {
            used += (size_t)snprintf(&line[used], sizeof(line) - used, " %s=%lu.%lu", foreground_report_columns[column].ShortName,
                                     (unsigned long)(share_x10 / 10U), (unsigned long)(share_x10 % 10U));
        }
    }
    if ((used < sizeof(line)) && (sum_x10 <= 1000U))
    {
        (void)snprintf(&line[used], sizeof(line) - used, " oth=%lu.%lu", (unsigned long)((1000U - sum_x10) / 10U),
                       (unsigned long)((1000U - sum_x10) % 10U));
    }
    foreground_logf("%s", line);

    foreground_report_store_baseline(count, total);
}

/**
 * @brief 输出 GUI 一行：窗口内循环数、flush 数、平均睡眠、最长帧。
 * @param[in] phase 阶段名。
 * @param[in] window_ms 本窗口经过的毫秒数。
 * @note  用来回答“唱盘不在屏幕上时是否还刷屏”：把 Now Playing 切走后看 flush 是否仍在增长。
 */
static void foreground_report_gui(const char *phase, uint32_t window_ms)
{
    GUI_TaskStatsTypeDef current;
    const uint32_t frequency_hz = CortexM7CycleCounter_GetFrequencyHz();
    uint32_t window_loops;
    uint32_t window_sleep_ms;
    uint32_t window_flush;
    uint32_t sleep_avg_x10;
    uint32_t frame_us;

    gui_task_get_stats(&current);
    window_loops = current.loops - foreground_report_previous_gui.loops;
    window_sleep_ms = current.sleep_ms_total - foreground_report_previous_gui.sleep_ms_total;
    window_flush = current.flush_count - foreground_report_previous_gui.flush_count;
    sleep_avg_x10 = (window_loops == 0U) ? 0U : (uint32_t)((uint64_t)window_sleep_ms * 10ULL / window_loops);
    frame_us = (frequency_hz == 0U) ? 0U
                                    : (uint32_t)((uint64_t)current.window_max_frame_cycles * 1000000ULL / frequency_hz);

    foreground_logf("GUI %s win=%lums loops=%lu flush=%lu sleep_avg=%lu.%lums frame_max=%luus sleep=%s", phase,
                    (unsigned long)window_ms, (unsigned long)window_loops, (unsigned long)window_flush,
                    (unsigned long)(sleep_avg_x10 / 10U), (unsigned long)(sleep_avg_x10 % 10U),
                    (unsigned long)frame_us, gui_task_get_sleep_enabled() ? "on" : "off");

    foreground_report_previous_gui = current;
    gui_task_stats_window_reset();
}

/**
 * @brief 输出一把锁在一个统计范围内的一行（最长持锁、最长等锁、拿锁次数）。
 * @param[in] lock_name 锁名。
 * @param[in] scope "ph" 为阶段窗口，"all" 为自启动。
 * @param[in] stats 该范围的统计。
 */
static void foreground_report_lock_line(const char *lock_name,
                                        const char *scope,
                                        const Service_Filesystem_SpikeLockStatsTypeDef *stats)
{
    char hold_task[FOREGROUND_REPORT_NAME_BYTES];
    char wait_task[FOREGROUND_REPORT_NAME_BYTES];

    foreground_report_clean_name(hold_task, stats->HoldTask);
    foreground_report_clean_name(wait_task, stats->WaitTask);
    foreground_logf("LOCK %s %s hold=%luus@%s/%s wait=%luus@%s/%s n=%lu", lock_name, scope,
                    (unsigned long)stats->HoldMaxUs, hold_task,
                    (stats->HoldOperation != NULL) ? stats->HoldOperation : "-",
                    (unsigned long)stats->WaitMaxUs, wait_task,
                    (stats->WaitOperation != NULL) ? stats->WaitOperation : "-",
                    (unsigned long)stats->AcquireCount);
}

/**
 * @brief 输出所有锁的阶段与自启动两组最长持锁与等锁，以及阶段内最长等锁时的持锁者。
 */
static void foreground_report_locks(void)
{
    uint32_t index;

    for (index = 0U; index < Service_Filesystem_SpikeLockCount(); index++)
    {
        Service_Filesystem_SpikeLockStatsTypeDef window;
        Service_Filesystem_SpikeLockStatsTypeDef total;
        const char *name = Service_Filesystem_SpikeLockName(index);

        if (!Service_Filesystem_SpikeGetLockStats(index, &window, &total))
        {
            continue;
        }
        foreground_report_lock_line(name, "ph", &window);
        if (window.BlockerOperation != NULL)
        {
            char blocker_task[FOREGROUND_REPORT_NAME_BYTES];

            foreground_report_clean_name(blocker_task, window.BlockerTask);
            foreground_logf("LOCKBLK %s ph longest_wait_blocked_by=%s/%s", name, blocker_task,
                            window.BlockerOperation);
        }
        foreground_report_lock_line(name, "all", &total);
    }
}

/** @brief 按操作分开的持有统计的临时拷贝，放静态区，不占前台任务的栈。 */
static Service_Filesystem_SpikeOpStatsTypeDef foreground_report_ops_window[SERVICE_FILESYSTEM_SPIKE_OP_MAX];
static Service_Filesystem_SpikeOpStatsTypeDef foreground_report_ops_total[SERVICE_FILESYSTEM_SPIKE_OP_MAX];

/** @brief 一行 LOCKOP 日志里允许写到的字节数，留出余量给 128 B 的单条上限。 */
#define FOREGROUND_REPORT_OPS_LINE_LIMIT 116U

/**
 * @brief 输出一把锁按操作分开的最长持有：若干个“操作=最长微秒/次数”挤在一行，放不下就换行。
 * @param[in] lock_name 锁名。
 * @param[in] scope "ph" 为阶段窗口，"all" 为自启动。
 * @param[in] table 长度 SERVICE_FILESYSTEM_SPIKE_OP_MAX 的统计表。
 * @note  没有拿锁记录的操作不输出。每条日志之后 `foreground_logf` 会让出几毫秒，不挤满 30 条的消息池。
 */
static void foreground_report_op_table(const char *lock_name,
                                       const char *scope,
                                       const Service_Filesystem_SpikeOpStatsTypeDef *table)
{
    char line[128];
    size_t prefix;
    size_t used;
    uint32_t index;

    prefix = (size_t)snprintf(line, sizeof(line), "LOCKOP %s %s", lock_name, scope);
    used = prefix;
    for (index = 0U; index < SERVICE_FILESYSTEM_SPIKE_OP_MAX; index++)
    {
        char item[48];
        size_t length;

        if ((table[index].Operation == NULL) || (table[index].Count == 0U))
        {
            continue;
        }
        length = (size_t)snprintf(item, sizeof(item), " %s=%luus/n%lu", table[index].Operation,
                                  (unsigned long)table[index].HoldMaxUs, (unsigned long)table[index].Count);
        if ((used > prefix) && ((used + length) > FOREGROUND_REPORT_OPS_LINE_LIMIT))
        {
            foreground_logf("%s", line);
            used = prefix;
            line[prefix] = '\0';
        }
        (void)snprintf(&line[used], sizeof(line) - used, "%s", item);
        used += length;
    }
    if (used > prefix)
    {
        foreground_logf("%s", line);
    }
}

/**
 * @brief 阶段结束时输出每把锁按操作分开的最长持有（#199）；打开表锁另给自启动那一份。
 */
static void foreground_report_ops(void)
{
    uint32_t index;

    for (index = 0U; index < Service_Filesystem_SpikeLockCount(); index++)
    {
        const char *name = Service_Filesystem_SpikeLockName(index);

        if (Service_Filesystem_SpikeGetOpStats(index, foreground_report_ops_window, foreground_report_ops_total) == 0U)
        {
            continue;
        }
        foreground_report_op_table(name, "ph", foreground_report_ops_window);
        if (strcmp(name, "TABLE") == 0)
        {
            foreground_report_op_table(name, "all", foreground_report_ops_total);
        }
    }
}

/**
 * @brief 输出欠载累计值与窗口增量。
 * @param[in] phase 阶段名。
 * @note  Platform 的计数在每次 Start 清零，累计在播放状态机里做。
 */
static void foreground_report_underruns(const char *phase)
{
    const uint32_t total = playback_task_get_underrun_total();

    foreground_logf("UNDER %s total=%lu win=%lu", phase, (unsigned long)total,
                    (unsigned long)(total - foreground_report_previous_underruns));
    foreground_report_previous_underruns = total;
}

/**
 * @brief 输出一个 ForegroundStat 的 n / min / avg / max 一行。
 * @param[in] key 行首关键字。
 * @param[in] phase 阶段名。
 * @param[in] item 项目名。
 * @param[in] stat 统计。
 */
static void foreground_report_stat_line(const char *key,
                                        const char *phase,
                                        const char *item,
                                        const ForegroundStatTypeDef *stat)
{
    foreground_logf("%s %s %s n=%lu min=%luus avg=%luus max=%luus", key, phase, item, (unsigned long)stat->Count,
                    (unsigned long)stat->MinUs, (unsigned long)foreground_stat_avg_us(stat),
                    (unsigned long)stat->MaxUs);
}

/**
 * @brief 输出 Flash 负载的块计数与擦除小结。
 * @param[in] phase 阶段名。
 * @param[in] load 负载统计。
 * @note  `blocks` 行给出空闲块与失效块的阶段开始值和当前值，以及 FTL 回收水位（空闲块数不高于 low 才开始擦，
 *        不低于 high 才停）；`erases` 行里 `phase` 是 FTL 擦除计数在本阶段的增量，`reclaim` 是其中回收调用里
 *        观察到的部分，`other` 是差值（写入路径里的前台回收，正常应为 0）；`reached` 为 no 时 `to_low` 是
 *        空闲块数离水位还差多少。
 */
static void foreground_report_flash_blocks(const char *phase, const ForegroundLoadStatsTypeDef *load)
{
    const Service_Filesystem_SpikeFlashBlocksTypeDef *start = &load->FlashBlocksStart;
    const Service_Filesystem_SpikeFlashBlocksTypeDef *now = &load->FlashBlocksNow;
    uint32_t phase_erases;
    uint32_t to_low;

    foreground_logf("FLASHIO %s files=%lu err=%lu", phase, (unsigned long)load->FlashFiles,
                    (unsigned long)load->FlashErrors);
    if (!load->FlashBlocksValid)
    {
        foreground_logf("FLASHIO %s blocks=unavailable", phase);
        return;
    }

    phase_erases = now->SessionErases - start->SessionErases;
    to_low = (now->FreeBlocks > load->FlashGcLow) ? (now->FreeBlocks - load->FlashGcLow) : 0U;
    foreground_logf("FLASHIO %s blocks free=%lu>%lu stale=%lu>%lu valid=%lu low=%lu high=%lu reserve=%lu", phase,
                    (unsigned long)start->FreeBlocks, (unsigned long)now->FreeBlocks,
                    (unsigned long)start->StaleBlocks, (unsigned long)now->StaleBlocks,
                    (unsigned long)now->ValidGroups, (unsigned long)load->FlashGcLow,
                    (unsigned long)load->FlashGcHigh, (unsigned long)start->ReserveBlocks);
    foreground_logf("FLASHIO %s erases phase=%lu reclaim=%lu other=%lu real_calls=%lu idle_calls=%lu reached=%s to_low=%lu",
                    phase, (unsigned long)phase_erases, (unsigned long)load->FlashErasesInReclaim,
                    (unsigned long)(phase_erases - load->FlashErasesInReclaim),
                    (unsigned long)load->FlashReclaimErase.Count, (unsigned long)load->FlashReclaimIdle.Count,
                    (load->FlashReclaimErase.Count != 0U) ? "yes" : "no", (unsigned long)to_low);
}

/**
 * @brief 输出 Flash 循环（#199）的统计：次数、各步骤的墙钟耗时、删除的卷锁持有、块计数。
 * @param[in] phase 阶段名。
 * @note  耗时都是墙钟（含等锁）；表锁最长持有按操作分开，见 LOCKOP 行。`partial` 为循环没写满目标就收尾的次数
 *        （空间不足，或 16 MiB 文件到了写入时限），此时 `last_bytes` 是最近一次删除的实际大小。
 */
static void foreground_report_cycle(const char *phase)
{
    const ForegroundCycleStatsTypeDef *cycle = foreground_cycle_stats();

    foreground_logf("CYCLE %s target=%lu cycles=%lu partial=%lu err=%lu last_bytes=%lu", phase,
                    (unsigned long)cycle->TargetBytes, (unsigned long)cycle->Cycles,
                    (unsigned long)cycle->PartialCycles, (unsigned long)cycle->Errors,
                    (unsigned long)cycle->LastBytes);
    foreground_report_stat_line("CYCLE", phase, "create", &cycle->Create);
    foreground_report_stat_line("CYCLE", phase, "write64K", &cycle->WriteChunk);
    foreground_report_stat_line("CYCLE", phase, "write_total", &cycle->WriteTotal);
    foreground_report_stat_line("CYCLE", phase, "sync", &cycle->Sync);
    foreground_report_stat_line("CYCLE", phase, "close", &cycle->Close);
    foreground_report_stat_line("CYCLE", phase, "rename", &cycle->Rename);
    foreground_report_stat_line("CYCLE", phase, "delete_wall", &cycle->Delete);
    foreground_report_stat_line("CYCLE", phase, "delete_hold", &cycle->DeleteHold);
    if (cycle->BlocksValid)
    {
        foreground_logf("CYCLE %s blocks free=%lu>%lu stale=%lu>%lu low=%lu erases=%lu", phase,
                        (unsigned long)cycle->BlocksStart.FreeBlocks, (unsigned long)cycle->BlocksNow.FreeBlocks,
                        (unsigned long)cycle->BlocksStart.StaleBlocks, (unsigned long)cycle->BlocksNow.StaleBlocks,
                        (unsigned long)cycle->GcLow,
                        (unsigned long)(cycle->BlocksNow.SessionErases - cycle->BlocksStart.SessionErases));
    }
}

/**
 * @brief 输出“大目录卡上按路径打开最后一首”（#199）的统计：目录规模、扫描耗时、打开的墙钟与 SD 卷锁持有。
 * @param[in] phase 阶段名。
 */
static void foreground_report_openlast(const char *phase)
{
    const ForegroundOpenLastStatsTypeDef *open_last = foreground_openlast_stats();

    foreground_logf("OPENLAST %s entries=%lu scan=%luus err=%lu last_status=%s last=%s", phase,
                    (unsigned long)open_last->Entries, (unsigned long)open_last->ScanUs,
                    (unsigned long)open_last->Errors, foreground_status_name(open_last->LastStatus),
                    open_last->Last);
    foreground_report_stat_line("OPENLAST", phase, "open_wall", &open_last->Open);
    foreground_report_stat_line("OPENLAST", phase, "open_sd_hold", &open_last->OpenHold);
    foreground_report_stat_line("OPENLAST", phase, "close", &open_last->Close);
}

/**
 * @brief 输出本阶段负载自己的统计行。
 * @param[in] phase 阶段名。
 * @param[in] kind 阶段的负载种类。
 */
static void foreground_report_load(const char *phase, ForegroundLoadKindTypeDef kind)
{
    const ForegroundLoadStatsTypeDef *load = foreground_load_stats();
    UiCellStatsTypeDef cell;

    switch (kind)
    {
        case FOREGROUND_LOAD_TXT:
            foreground_report_stat_line("SDREAD", phase, "fg32K", &load->Txt32k);
            foreground_logf("SDREAD %s fg4K n=%lu avg=%luus max=%luus err=%lu last=%s nl=%lu pass=%lu", phase,
                            (unsigned long)load->Txt4k.Count, (unsigned long)foreground_stat_avg_us(&load->Txt4k),
                            (unsigned long)load->Txt4k.MaxUs, (unsigned long)load->TxtErrors,
                            foreground_status_name(load->TxtLastStatus), (unsigned long)load->TxtNewlines,
                            (unsigned long)load->TxtPasses);
            break;

        case FOREGROUND_LOAD_FLASH:
            foreground_report_stat_line("FLASHIO", phase, "write64K", &load->FlashWrite);
            foreground_report_stat_line("FLASHIO", phase, "sync", &load->FlashSync);
            foreground_report_stat_line("FLASHIO", phase, "reclaim_erase", &load->FlashReclaimErase);
            foreground_report_stat_line("FLASHIO", phase, "reclaim_idle", &load->FlashReclaimIdle);
            foreground_report_flash_blocks(phase, load);
            break;

        case FOREGROUND_LOAD_FLASH_CYCLE_1M:
        case FOREGROUND_LOAD_FLASH_CYCLE_16M:
            foreground_report_cycle(phase);
            break;

        case FOREGROUND_LOAD_SD_OPEN_LAST:
            foreground_report_openlast(phase);
            break;

        case FOREGROUND_LOAD_CELL:
            ui_cell_stats_get(foreground_load_cell(), &cell);
            foreground_logf("CELL %s write n=%lu avg=%luus max=%luus (synthetic 6KiB)", phase,
                            (unsigned long)cell.WriteCount,
                            (unsigned long)((cell.WriteCount == 0U) ? 0U : (cell.WriteSumUs / cell.WriteCount)),
                            (unsigned long)cell.WriteMaxUs);
            foreground_logf("CELL %s read n=%lu avg=%luus max=%luus (synthetic 6KiB)", phase,
                            (unsigned long)cell.ReadCount,
                            (unsigned long)((cell.ReadCount == 0U) ? 0U : (cell.ReadSumUs / cell.ReadCount)),
                            (unsigned long)cell.ReadMaxUs);
            foreground_logf("CELLGUI %s try=%lu fail=%lu (synthetic, reference only)", phase,
                            (unsigned long)cell.ReadAttempts, (unsigned long)cell.ReadLockFails);
            break;

        default:
            break;
    }
}

/**
 * @brief 输出一个窗口（或阶段结束）的全部汇总行。
 * @param[in] phase 阶段名，如 "P2"。
 * @param[in] kind 阶段的负载种类，决定输出哪组负载统计。
 * @param[in] final true 为阶段结束的汇总：GUARD 行无论是否为 0 都输出。
 * @note  CPU 与 GUI 是本窗口（自上一次汇总以来）的差值；LOCK 的 ph 组与负载统计是自阶段开始以来的累计。
 */
void foreground_report_window(const char *phase, ForegroundLoadKindTypeDef kind, bool final)
{
    const TickType_t now = xTaskGetTickCount();
    const uint32_t window_ms = (uint32_t)(now - foreground_report_previous_tick) * (uint32_t)portTICK_PERIOD_MS;
    const uint32_t guard = Service_Filesystem_SpikeForbiddenCalls();
    ForegroundStatTypeDef audio_read;

    foreground_load_refresh(kind);
    foreground_report_cpu(phase, window_ms);
    foreground_report_gui(phase, window_ms);
    foreground_report_locks();
    if (final)
    {
        foreground_report_ops();
    }
    spike_audio_read_get(&audio_read);
    foreground_report_stat_line("SDREAD", phase, "audio32K", &audio_read);
    foreground_report_underruns(phase);
    foreground_report_load(phase, kind);
    if (final || (guard != 0U))
    {
        foreground_logf("GUARD %s fs_calls_from_gui_or_playback=%lu", phase, (unsigned long)guard);
    }

    foreground_report_previous_tick = now;
}

/**
 * @brief 格式化一个阶段时刻：已到达为 "+<ms>"，未到达为 "-"。
 * @param[out] text 至少 12 字节。
 * @param[in] value 相对检测中断的毫秒，或 UINT32_MAX。
 */
static void foreground_report_stage_text(char *text, uint32_t value)
{
    if (value == UINT32_MAX)
    {
        (void)snprintf(text, 12U, "-");
    }
    else
    {
        (void)snprintf(text, 12U, "+%lu", (unsigned long)value);
    }
}

/**
 * @brief 若拔卡时间线已完整，输出它。
 * @note  所有时刻是相对检测中断的毫秒，起点是第一个边沿的 tick（ISR 里只存 tick）。
 *        卸载要先拿 SD 卷锁，所以 unmount_done 与 unmount_enter 之差包含等在途读取的时间；
 *        前台任务那次读取的持锁时长说明了它在锁里待了多久。
 */
void foreground_report_unplug(void)
{
    SpikeUnplugReportTypeDef report;
    char debounce[12];
    char stream[12];
    char catalog[12];
    char enter[12];
    char done[12];

    if (!spike_unplug_take_report(&report))
    {
        return;
    }

    foreground_report_stage_text(debounce, report.StageMs[SPIKE_UNPLUG_STAGE_DEBOUNCE_END]);
    foreground_report_stage_text(stream, report.StageMs[SPIKE_UNPLUG_STAGE_STREAM_CLOSED]);
    foreground_report_stage_text(catalog, report.StageMs[SPIKE_UNPLUG_STAGE_CATALOG_INVALID]);
    foreground_report_stage_text(enter, report.StageMs[SPIKE_UNPLUG_STAGE_UNMOUNT_ENTER]);
    foreground_report_stage_text(done, report.StageMs[SPIKE_UNPLUG_STAGE_UNMOUNT_DONE]);
    foreground_logf("UNPLUG via=%s detect_isr=0 debounce_end=%s stream_closed=%s catalog_invalid=%s",
                    (report.Via == SPIKE_UNPLUG_VIA_STATE)      ? "state"
                    : (report.Via == SPIKE_UNPLUG_VIA_PRESENCE) ? "presence"
                                                                : "insert",
                    debounce, stream, catalog);
    foreground_logf("UNPLUG unmount_enter=%s unmount_done=%s unmount_status=%s (ms from detect ISR)", enter, done,
                    (report.UnmountStatus == 0U) ? "OK" : "ERROR");
    if (report.ForegroundRead)
    {
        foreground_logf("UNPLUG fg_read status=%s returned_at=+%lums in_lock=%luus",
                        foreground_status_name(report.ForegroundStatus), (unsigned long)report.ForegroundMs,
                        (unsigned long)report.ForegroundHoldUs);
    }
    else
    {
        foreground_logf("UNPLUG fg_read=none within 3000ms (no TXT read failed after detect)");
    }
}
