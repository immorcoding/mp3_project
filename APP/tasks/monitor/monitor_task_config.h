/**
  ******************************************************************************
  * @file    monitor_task_config.h
  * @brief   Monitor Task 的私有周期与输出缓冲参数。
  ******************************************************************************
  */

#ifndef MONITOR_TASK_CONFIG_H
#define MONITOR_TASK_CONFIG_H

/** @brief Monitor Task 的基础调度周期，单位为毫秒。 */
#define MONITOR_TASK_PERIOD_MS           500U
/** @brief 经过多少个基础周期后输出一次任务栈快照。 */
#define MONITOR_SNAPSHOT_INTERVAL         10U
/** @brief 单条任务栈监控日志的本地格式化缓冲区长度。 */
#define MONITOR_LOG_MESSAGE_LENGTH        64U

#endif /* MONITOR_TASK_CONFIG_H */
