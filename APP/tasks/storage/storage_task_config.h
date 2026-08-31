/**
 ******************************************************************************
 * @file    storage_task_config.h
 * @brief   Storage Task 的私有热插拔调度参数。
 ******************************************************************************
 */

#ifndef STORAGE_TASK_CONFIG_H
#define STORAGE_TASK_CONFIG_H

/** @brief SD 卡检测输入在最后一个边沿后的最短稳定时间，单位为毫秒。 */
#define STORAGE_SD_DEBOUNCE_MS 30U

/* 空闲时每 100 ms 提供一次有限 GC 机会；不代表每次都会擦除。 */
#define STORAGE_FLASH_MAINTENANCE_PERIOD_MS 100U

#endif /* STORAGE_TASK_CONFIG_H */
