/**
  ******************************************************************************
  * @file    cortex_m7_dcache_adapter.h
 * @brief   Cortex-M7 D-Cache 按范围维护的公共 Adapter Interface。
  *
  * @details
 *          本 Module 只维护调用者给定内存范围的 D-Cache，不持有 DMA、外设 Handle、
 *          传输状态或缓冲区所有权。DMA Adapter、SDRAM 诊断等调用者按自身时机选择
 *          Clean、Invalidate 或 Clean + Invalidate。
  ******************************************************************************
  */

#ifndef CORTEX_M7_DCACHE_ADAPTER_H
#define CORTEX_M7_DCACHE_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Cortex-M7 一级数据 Cache 的单条 Cache line 大小，单位为字节。
 * @note  DMA 缓冲区的首地址和有效长度都必须是该值的整数倍，避免 Cache 维护
 *        操作扩大到相邻变量所在的 Cache line。
 */
#define CORTEX_M7_DCACHE_LINE_SIZE  32U

bool CortexM7DCache_CleanRange(const void *buffer, uint32_t byte_count);
bool CortexM7DCache_InvalidateRange(const void *buffer, uint32_t byte_count);
bool CortexM7DCache_CleanInvalidateRange(const void *buffer,
                                          uint32_t byte_count);

#endif /* CORTEX_M7_DCACHE_ADAPTER_H */
