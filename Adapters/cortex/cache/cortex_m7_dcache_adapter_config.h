/**
  ******************************************************************************
  * @file    cortex_m7_dcache_adapter_config.h
  * @brief   Cortex-M7 D-Cache Adapter 的行大小。
  *
  * @details
  *          只发布 Cache line 字节数，不含 Clean/Invalidate。Platform 可包含本头
  *          以别名 `PLATFORM_DCACHE_LINE_SIZE`；需要维护 Cache 的调用者仍包含
  *          `cortex_m7_dcache_adapter.h`。
  ******************************************************************************
  */

#ifndef CORTEX_M7_DCACHE_ADAPTER_CONFIG_H
#define CORTEX_M7_DCACHE_ADAPTER_CONFIG_H

/* cortex_m7_dcache_adapter.c */
#define CORTEX_M7_DCACHE_LINE_SIZE  32U  /* Cortex-M7 D-Cache line，单位为字节。 */

#endif /* CORTEX_M7_DCACHE_ADAPTER_CONFIG_H */
