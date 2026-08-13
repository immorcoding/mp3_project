/**
  ******************************************************************************
  * @file    st7789_spi_stm32_hal_adapter.h
  * @brief   STM32 HAL SPI/GPIO 到 ST7789 Device Interface 的 Adapter。
  ******************************************************************************
  */

#ifndef ST7789_SPI_STM32_HAL_ADAPTER_H
#define ST7789_SPI_STM32_HAL_ADAPTER_H

#include <stdint.h>

#include "Components/st7789/st7789.h"
#include "stm32h7xx_hal.h"

/**
  * @brief STM32 HAL SPI 和 LCD 三条控制线的具体 Adapter Context。
  * @note  Context 由 Platform 长期持有，只借用 CubeMX 创建的 SPI Handle 和
  *        GPIO Port。三个 ActiveState 由 Platform 指定，因此 Adapter 本身
  *        不假设具体 PCB 的低有效或高有效电平。
  */
typedef struct
{
    SPI_HandleTypeDef *SPIHandle; /**< CubeMX 生成的 SPI Handle。 */
    GPIO_TypeDef *ChipSelectPort; /**< LCD CS 所在 GPIO Port。 */
    uint16_t ChipSelectPin;       /**< LCD CS 引脚掩码。 */
    GPIO_PinState ChipSelectActiveState; /**< 选中 LCD 时的 CS 电平。 */
    GPIO_TypeDef *DataCommandPort; /**< LCD D/C 所在 GPIO Port。 */
    uint16_t DataCommandPin;       /**< LCD D/C 引脚掩码。 */
    GPIO_PinState CommandState;    /**< D/C 表示命令时的电平。 */
    GPIO_TypeDef *ResetPort;       /**< LCD RESET 所在 GPIO Port。 */
    uint16_t ResetPin;             /**< LCD RESET 引脚掩码。 */
    GPIO_PinState ResetAssertState; /**< RESET 有效时的电平。 */
    uint32_t TimeoutMs;            /**< 单次 HAL SPI 阻塞传输超时，单位为毫秒。 */
} ST7789_SPI_STM32HALAdapterTypeDef;

ST7789_StatusTypeDef ST7789_SPI_STM32HALAdapter_Bind(
    ST7789_HandleTypeDef *hst7789,
    ST7789_SPI_STM32HALAdapterTypeDef *adapter);

#endif /* ST7789_SPI_STM32_HAL_ADAPTER_H */
