/**
  ******************************************************************************
  * @file    sd_stm32_hal_adapter_config.h
  * @brief   STM32 HAL SDMMC Adapter 的私有 DMA 参数。
  *
  * @details
  *          本文件只约束当前 HAL SDMMC 后端的 DMA 缓冲区字节数计算，
  *          不属于 SD Card Component 或 Platform SD 的公开 Interface。
  ******************************************************************************
  */

#ifndef SDCARD_STM32_HAL_ADAPTER_CONFIG_H
#define SDCARD_STM32_HAL_ADAPTER_CONFIG_H

/** @brief SDMMC HAL DMA 使用的固定逻辑块大小，单位为字节。 */
#define SD_STM32_HAL_DMA_LOGICAL_BLOCK_SIZE  512U

#endif /* SDCARD_STM32_HAL_ADAPTER_CONFIG_H */
