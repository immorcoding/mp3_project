/**
  ******************************************************************************
  * @file    st7789_spi_stm32_hal_adapter_config.h
  * @brief   STM32 HAL ST7789 SPI Adapter 的私有 DMA 配置。
  ******************************************************************************
  */

#ifndef ST7789_SPI_STM32_HAL_ADAPTER_CONFIG_H
#define ST7789_SPI_STM32_HAL_ADAPTER_CONFIG_H

/**
 * @brief 单次 HAL SPI DMA 请求的最大 RGB565 像素数。
 * @note  Adapter 在像素 DMA 阶段将 SPI 切换到 16-bit 串行帧，HAL 的 Size 参数
 *        此时以半字（一个 RGB565 像素）为单位。本值必须不大于 `UINT16_MAX`；
 *        60000 既保留余量，又与已验证的参考工程分块规模一致。
 */
#define ST7789_SPI_STM32_HAL_DMA_CHUNK_PIXELS  60000u

#endif /* ST7789_SPI_STM32_HAL_ADAPTER_CONFIG_H */
