/**
  ******************************************************************************
  * @file    monitor_warn.c
  * @brief   Monitor Task 未登记任务告警的去重记录实现。
  ******************************************************************************
  */

#include "APP/tasks/monitor/monitor_warn.h"

#include <string.h>

/**
  * @brief  清空告警记录。
  * @param  record 待初始化的记录；NULL 时无动作。
  */
void monitor_warn_record_init(monitor_warn_record_t *record)
{
    if (record == NULL)
    {
        return;
    }

    (void)memset(record, 0, sizeof(*record));
}

/**
  * @brief  判断是否应为该未登记任务名发出告警，并在需要时登记。
  * @param  record 告警记录。
  * @param  task_name 任务名；超过 MONITOR_WARNED_NAME_LENGTH - 1 的部分被截断后比较。
  * @retval true 该名字首次出现且记录有空位，已登记，调用方应发出一条告警。
  * @retval false 已告警过、记录已满、或参数为 NULL/空名字，调用方保持沉默。
  */
bool monitor_warn_should_report(monitor_warn_record_t *record, const char *task_name)
{
    if ((record == NULL) || (task_name == NULL) || (task_name[0] == '\0'))
    {
        return false;
    }

    for (uint8_t index = 0U; index < record->count; index++)
    {
        if (strncmp(record->names[index], task_name, MONITOR_WARNED_NAME_LENGTH - 1U) == 0)
        {
            return false;
        }
    }

    if (record->count >= MONITOR_WARNED_TASK_MAX)
    {
        return false;
    }

    /* 记录已被 init 清零，复制至多 LENGTH-1 字节即保证 NUL 结尾。 */
    (void)strncpy(record->names[record->count], task_name, MONITOR_WARNED_NAME_LENGTH - 1U);
    record->count++;
    return true;
}
