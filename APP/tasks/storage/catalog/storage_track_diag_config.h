/**
 * @file storage_track_diag_config.h
 * @brief 标签读取诊断的配置常量：超预算阈值。
 */

#ifndef STORAGE_TRACK_DIAG_CONFIG_H
#define STORAGE_TRACK_DIAG_CONFIG_H

#include "APP/tasks/playback/playback_task_config.h"

/* storage_track_diag.c：单行标签读取的预算阈值，由 T_data 的现有配置值推出。判定公式见 #135 I 节：
 * 单行标签读取的上界 < Storage 单圈余量 T_data/2（板测清单 B8），超过即在窗口汇总行中记出。 */
#define STORAGE_TRACK_DIAG_BUDGET_US   ((PLAYBACK_MACHINE_DATA_TIMEOUT_MS * 1000U) / 2U) /* 单行标签读取耗时的超预算阈值，单位为微秒。 */

#endif /* STORAGE_TRACK_DIAG_CONFIG_H */
