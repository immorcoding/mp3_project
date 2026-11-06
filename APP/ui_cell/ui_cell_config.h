/**
 * @file ui_cell_config.h
 * @brief ui_cell 的持锁统计开关、汇总节拍与汇总行容量。
 */

#ifndef UI_CELL_CONFIG_H
#define UI_CELL_CONFIG_H

/* ui_cell_stats.h、ui_cell.c：持锁统计（DIAG-6；与 Filesystem 的持锁统计、曲库短锁统计同一个开关口径：Debug 开、Release 关）。
 * 固件 Release 带 -DNDEBUG，整段编译掉：不量时间、不加计数、不投日志，也不占字段。 */
#ifndef NDEBUG
#define UI_CELL_LOCK_STATS_ENABLE        1U
#else
#define UI_CELL_LOCK_STATS_ENABLE        0U
#endif
#define UI_CELL_LOCK_STATS_PERIOD_MS     10000U  /* 汇总日志的节拍：前台任务每圈调一次 `ui_cell_stats_poll()`，距上一行已满此时长且有新数据才投一行（共 1 行）。 */
#define UI_CELL_LOCK_STATS_LINE_BYTES    96U     /* 汇总行容量（含结尾 NUL）；`n=<10 位> hold=<10 位>us gui=<10 位> busy=<10 位> pct=100.0` 最长 70 字符。 */

#endif /* UI_CELL_CONFIG_H */
