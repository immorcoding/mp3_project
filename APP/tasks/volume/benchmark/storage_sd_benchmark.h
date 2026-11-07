/**
  ******************************************************************************
  * @file    storage_sd_benchmark.h
  * @brief   Storage Task 内部的 SD 文件系统读写测速入口。
  ******************************************************************************
  */

#ifndef STORAGE_SD_BENCHMARK_H
#define STORAGE_SD_BENCHMARK_H

#include "APP/app_config.h"
#if STORAGE_SD_BENCHMARK_ENABLE

#include <stdbool.h>

bool storage_sd_benchmark_run(void);

#endif /* STORAGE_SD_BENCHMARK_ENABLE */

#endif /* STORAGE_SD_BENCHMARK_H */
