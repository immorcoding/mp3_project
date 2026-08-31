/**
 * @file FreeRTOS.h
 * @brief 主机文件基准最小 tick 类型，不模拟真实调度器。
 */
#ifndef FILE_BENCH_TEST_FREERTOS_H
#define FILE_BENCH_TEST_FREERTOS_H
#include <stdint.h>
typedef uint32_t TickType_t;
#define configTICK_RATE_HZ 1000U
#endif
