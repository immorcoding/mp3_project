/**
  ******************************************************************************
  * @file    st7789_spi_stm32_hal_adapter_config.h
  * @brief   STM32 HAL ST7789 SPI Adapter 的私有 DMA 配置。
  ******************************************************************************
  */

#ifndef ST7789_SPI_STM32_HAL_ADAPTER_CONFIG_H
#define ST7789_SPI_STM32_HAL_ADAPTER_CONFIG_H

/* st7789_spi_stm32_hal_adapter.c */
#define ST7789_SPI_STM32_HAL_DMA_CHUNK_PIXELS  60000u  /* 单次 HAL SPI DMA 最大 RGB565 像素数；须不大于 UINT16_MAX，与已验证分块规模一致。 */

#endif /* ST7789_SPI_STM32_HAL_ADAPTER_CONFIG_H */
