/**
 * @file ui_cell_stats.h
 * @brief ui_cell 持锁统计的内部接口：持锁最长、GUI 一侧不等入口的尝试与拿不到的次数（只给 `ui_cell.c` 和本模块的主机测试用）。
 *
 * @details
 *          统计是证据出口（R5.3），Debug 开、Release 关（`UI_CELL_LOCK_STATS_ENABLE`）。关闭时下面几个入口是空的内联函数，
 *          调用点不用写 `#if`，也不量时间、不加计数。读累计值和汇总日志的入口在 `ui_cell.h`（`ui_cell_stats_get()`、
 *          `ui_cell_stats_poll()`），它们在两种构建里都存在，关闭时分别返回全 0 和什么都不做。
 *          持锁时间只含锁内的区间（`memcpy` 与序号推进），不含叫醒函数和断言，所以量的是别的任务等这把锁的最长时间上界。
 */

#ifndef UI_CELL_STATS_H
#define UI_CELL_STATS_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/ui_cell/ui_cell_config.h"

#if (UI_CELL_LOCK_STATS_ENABLE != 0U)

/* 拿到锁之后立刻调：记下起点周期数并把拿取次数加一。 */
uint32_t ui_cell_stats_hold_begin(void);

/* 放锁之前调：用起点算这一次的持有时间（微秒），刷新持锁最长值。 */
void ui_cell_stats_hold_end(uint32_t start_cycles);

/* GUI 一侧的不等入口每尝试一次调一次；busy 为真表示没拿到锁（下一帧再试）。 */
void ui_cell_stats_gui_try(bool busy);

#else

/**
 * @brief 发布构建里持锁统计整段编译掉：什么都不量。
 * @return 恒为 0。
 */
static inline uint32_t ui_cell_stats_hold_begin(void)
{
    return 0U;
}

/**
 * @brief 发布构建里持锁统计整段编译掉：什么都不记。
 * @param[in] start_cycles 未使用。
 */
static inline void ui_cell_stats_hold_end(uint32_t start_cycles)
{
    (void)start_cycles;
}

/**
 * @brief 发布构建里持锁统计整段编译掉：什么都不记。
 * @param[in] busy 未使用。
 */
static inline void ui_cell_stats_gui_try(bool busy)
{
    (void)busy;
}

#endif /* UI_CELL_LOCK_STATS_ENABLE */

#endif /* UI_CELL_STATS_H */
