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
 * @note  严格对齐 Interface 要求首地址和有效长度均为该值的整数倍，避免 Cache
 *        维护操作扩大到相邻变量所在的 Cache line。
 */
#define CORTEX_M7_DCACHE_LINE_SIZE  32U

/* 严格对齐：首地址与长度都必须按 Cache line 对齐。 */
bool CortexM7DCache_Clean_Aligned(const void *buffer, uint32_t byte_count);
bool CortexM7DCache_Invalidate_Aligned(const void *buffer,
                                        uint32_t byte_count);
bool CortexM7DCache_CleanInvalidate_Aligned(const void *buffer,
                                             uint32_t byte_count);

/* 尾部补齐：首地址必须按 Cache line 对齐，Clean 会向后覆盖至 line 末尾。 */
bool CortexM7DCache_Clean_Rounded(const void *buffer, uint32_t byte_count);

#endif /* CORTEX_M7_DCACHE_ADAPTER_H */
