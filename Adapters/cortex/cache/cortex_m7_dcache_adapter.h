/**
  ******************************************************************************
  * @file    cortex_m7_dcache_adapter.h
  * @brief   Cortex-M7 D-Cache 与 DMA 缓冲区一致性的公共 Adapter Interface。
  *
  * @details
  *          本 Module 只维护调用者给定内存范围的 D-Cache 一致性，不持有 DMA、
  *          外设 Handle、传输状态或缓冲区所有权。SDMMC、SPI、I2S 等具体 Adapter
  *          负责在合适的传输开始/结束时机调用本 Interface。
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

bool CortexM7DCache_PrepareDMARx(void *buffer, uint32_t byte_count);
bool CortexM7DCache_CompleteDMARx(void *buffer, uint32_t byte_count);
bool CortexM7DCache_PrepareDMATx(const void *buffer, uint32_t byte_count);

#endif /* CORTEX_M7_DCACHE_ADAPTER_H */
