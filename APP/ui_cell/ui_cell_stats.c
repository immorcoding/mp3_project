/**
 * @file ui_cell_stats.c
 * @brief ui_cell 持锁统计：持锁最长、GUI 一侧不等入口的尝试与拿不到的次数，以及汇总行（DIAG-6、R5.3）。
 *
 * @details
 *          Debug 开、Release 关（`UI_CELL_LOCK_STATS_ENABLE`），关闭时本文件只剩 `ui_cell_stats_get()` 和 `ui_cell_stats_poll()`
 *          两个空壳。并发：拿取次数和持锁最长值只在锁内写（读者任务不同，但先后不重叠）；GUI 的尝试与 busy 计数只由 GUI 任务写；
 *          汇总只由前台任务调用 `ui_cell_stats_poll()` 读，32 位对齐读写在 Cortex-M7 上不撕裂，读到半新半旧的几个数
 *          只影响一行诊断，不影响数据。汇总日志只在任务里投，不在 GUI 帧路径上投（DIAG-3）。
 */

#include "APP/ui_cell/ui_cell.h"
#include "APP/ui_cell/ui_cell_stats.h"

#include <stdio.h>
#include <string.h>

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Service/log/log_service.h"

#if (UI_CELL_LOCK_STATS_ENABLE != 0U)

static const char ui_cell_stats_tag[] = "UICELL";

/** @brief 开机以来的累计值（最大值不清零）；位于 .bss，上电为零。 */
static UiCell_StatsTypeDef ui_cell_stats_totals;

/** @brief 上一行汇总所含的累计值：和现在不同才说明有新数据。 */
static UiCell_StatsTypeDef ui_cell_stats_reported;

/** @brief 上一行汇总的毫秒时刻（从 0 起算：开机后第一个周期满了才投第一行）。 */
static uint32_t ui_cell_stats_last_report_ms;

/**
 * @brief 把周期数换成微秒。
 * @param[in] cycles 周期数。
 * @return 微秒；计数器没启动（频率不足 1 MHz）时为 0。
 */
static uint32_t ui_cell_stats_cycles_to_us(uint32_t cycles)
{
    const uint32_t cycles_per_us = CortexM7CycleCounter_GetFrequencyHz() / 1000000U;

    return (cycles_per_us == 0U) ? 0U : (cycles / cycles_per_us);
}

/**
 * @brief 拿到锁之后调：把拿取次数加一，返回起点周期数。
 * @return 起点周期数，交给 `ui_cell_stats_hold_end()`。
 * @note  调用者已持锁，所以拿取次数的读-改-写不会和别的任务重叠。
 */
uint32_t ui_cell_stats_hold_begin(void)
{
    ui_cell_stats_totals.Acquisitions++;
    return CortexM7CycleCounter_Read();
}

/**
 * @brief 放锁之前调：算这一次的持有时间，刷新持锁最长值。
 * @param[in] start_cycles `ui_cell_stats_hold_begin()` 的返回值。
 * @note  周期数用无符号差，32 位回绕不影响。调用者仍持锁。
 */
void ui_cell_stats_hold_end(uint32_t start_cycles)
{
    const uint32_t hold_us = ui_cell_stats_cycles_to_us(CortexM7CycleCounter_Read() - start_cycles);

    if (hold_us > ui_cell_stats_totals.HoldMaxUs)
    {
        ui_cell_stats_totals.HoldMaxUs = hold_us;
    }
}

/**
 * @brief GUI 一侧的不等入口每尝试一次调一次。
 * @param[in] busy 为真表示没拿到锁。
 * @note  只由 GUI 任务调用，所以两个计数不需要锁；本函数不拿锁、不投日志，可以在帧路径上调用。
 */
void ui_cell_stats_gui_try(bool busy)
{
    ui_cell_stats_totals.GuiTries++;
    if (busy)
    {
        ui_cell_stats_totals.GuiBusy++;
    }
}

/**
 * @brief 读累计值。
 * @param[out] out 累计值。
 */
void ui_cell_stats_get(UiCell_StatsTypeDef *out)
{
    *out = ui_cell_stats_totals;
}

/**
 * @brief 前台任务每圈调一次：距上一行已满一个周期且有新数据时投一行汇总。
 * @param[in] now_ms 当前毫秒时刻（FreeRTOS tick；无符号差处理回绕）。
 * @note  汇总行 `n=<拿取次数> hold=<持锁最长 µs>us gui=<GUI 不等入口的尝试次数> busy=<其中没拿到锁的次数> pct=<busy 占比，一位小数>`，
 *        都是开机以来的累计值（最大值不清零）。占比 = busy / gui，向下取整到 0.1 %。至多每 10 s 一行，没有新数据不投，
 *        所以不占 Service Log 消息池的稳态余量（DIAG-3）。
 */
void ui_cell_stats_poll(uint32_t now_ms)
{
    const UiCell_StatsTypeDef current = ui_cell_stats_totals;
    char line[UI_CELL_LOCK_STATS_LINE_BYTES];
    uint32_t tenths = 0U;

    if (((uint32_t)(now_ms - ui_cell_stats_last_report_ms) < (uint32_t)UI_CELL_LOCK_STATS_PERIOD_MS) ||
        (memcmp(&current, &ui_cell_stats_reported, sizeof(current)) == 0))
    {
        return;
    }

    if (current.GuiTries != 0U)
    {
        tenths = (uint32_t)(((uint64_t)current.GuiBusy * 1000ULL) / (uint64_t)current.GuiTries);
    }
    (void)snprintf(line, sizeof(line), "n=%lu hold=%luus gui=%lu busy=%lu pct=%lu.%lu",
                   (unsigned long)current.Acquisitions,
                   (unsigned long)current.HoldMaxUs,
                   (unsigned long)current.GuiTries,
                   (unsigned long)current.GuiBusy,
                   (unsigned long)(tenths / 10U),
                   (unsigned long)(tenths % 10U));

    ui_cell_stats_reported = current;
    ui_cell_stats_last_report_ms = now_ms;
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, ui_cell_stats_tag, line);
}

#else

/**
 * @brief 发布构建：持锁统计关闭，累计值恒为 0。
 * @param[out] out 清零。
 */
void ui_cell_stats_get(UiCell_StatsTypeDef *out)
{
    (void)memset(out, 0, sizeof(*out));
}

/**
 * @brief 发布构建：持锁统计关闭，什么都不投。
 * @param[in] now_ms 未使用。
 */
void ui_cell_stats_poll(uint32_t now_ms)
{
    (void)now_ms;
}

#endif /* UI_CELL_LOCK_STATS_ENABLE */
