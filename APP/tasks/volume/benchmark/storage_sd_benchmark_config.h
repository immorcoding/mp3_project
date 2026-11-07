/**
  ******************************************************************************
  * @file    storage_sd_benchmark_config.h
  * @brief   Storage Task SD 端到端测速的私有数据规模参数。
  *
  * @details
  *          本文件只控制 APP 诊断测试，不影响 Filesystem Service 的实际 DMA
  *          中转分块策略。启用与否仍由 `APP/app_config.h` 管理。
  ******************************************************************************
  */

#ifndef STORAGE_SD_BENCHMARK_CONFIG_H
#define STORAGE_SD_BENCHMARK_CONFIG_H

/* storage_sd_benchmark.c */
#define STORAGE_SD_BENCHMARK_TOTAL_BYTES     (64U * 1024U * 1024U)  /* 测试文件总有效载荷，64 MiB 用于稀释创建和关闭的固定开销。 */
#define STORAGE_SD_BENCHMARK_CHUNK_BYTES     (32U * 1024U)  /* 单次 WriteFile 的应用层缓冲区长度，单位为字节。 */
#define STORAGE_SD_BENCHMARK_CHUNK_COUNT     (STORAGE_SD_BENCHMARK_TOTAL_BYTES / STORAGE_SD_BENCHMARK_CHUNK_BYTES)  /* 按总分块得到的写入次数。 */
#define STORAGE_SD_BENCHMARK_SEQUENCE_BYTES  4U             /* 每个应用层缓冲区头部用于标记写入顺序的字节数。 */

#endif /* STORAGE_SD_BENCHMARK_CONFIG_H */
