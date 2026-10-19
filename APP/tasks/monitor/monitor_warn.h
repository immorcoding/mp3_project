/**
  ******************************************************************************
  * @file    monitor_warn.h
  * @brief   Monitor Task 未登记任务告警的去重记录（纯 C，无 RTOS 依赖）。
  ******************************************************************************
  */

#ifndef MONITOR_WARN_H
#define MONITOR_WARN_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/tasks/monitor/monitor_task_config.h"

/** 已告警任务名的固定容量记录；由调用方持有，不动态分配。 */
typedef struct
{
    char names[MONITOR_WARNED_TASK_MAX][MONITOR_WARNED_NAME_LENGTH]; /**< 已告警的任务名（可能被截断）。 */
    uint8_t count;                                                   /**< 已使用的记录数。 */
} monitor_warn_record_t;

/**
  * @brief  清空告警记录。
  * @param  record 待初始化的记录；NULL 时无动作。
  */
void monitor_warn_record_init(monitor_warn_record_t *record);

/**
  * @brief  判断是否应为该未登记任务名发出告警，并在需要时登记。
  * @param  record 告警记录。
  * @param  task_name 任务名；超过 MONITOR_WARNED_NAME_LENGTH - 1 的部分被截断后比较。
  * @retval true 该名字首次出现且记录有空位，已登记，调用方应发出一条告警。
  * @retval false 已告警过、记录已满、或参数为 NULL/空名字，调用方保持沉默。
  */
bool monitor_warn_should_report(monitor_warn_record_t *record, const char *task_name);

#endif /* MONITOR_WARN_H */
