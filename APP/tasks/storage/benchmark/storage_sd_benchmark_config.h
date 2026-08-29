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

/** @brief 测试文件写入的总有效载荷，64 MiB 足以稀释文件创建和关闭的固定开销。 */
#define STORAGE_SD_BENCHMARK_TOTAL_BYTES       (64U * 1024U * 1024U)
/** @brief 单次 FatFs f_write() 的应用层缓冲区长度，单位为字节。 */
#define STORAGE_SD_BENCHMARK_CHUNK_BYTES       (32U * 1024U)
/** @brief 总有效载荷按固定应用层缓冲区分割后的块数。 */
#define STORAGE_SD_BENCHMARK_CHUNK_COUNT       \
    (STORAGE_SD_BENCHMARK_TOTAL_BYTES / STORAGE_SD_BENCHMARK_CHUNK_BYTES)
/** @brief 每个应用层缓冲区头部用于标记写入顺序的字节数。 */
#define STORAGE_SD_BENCHMARK_SEQUENCE_BYTES    4U

#endif /* STORAGE_SD_BENCHMARK_CONFIG_H */
