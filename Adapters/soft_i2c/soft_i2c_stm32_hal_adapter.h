#ifndef SOFT_I2C_STM32_HAL_ADAPTER_H
#define SOFT_I2C_STM32_HAL_ADAPTER_H

#include <stdint.h>

#include "Components/soft_i2c/soft_i2c.h"
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief STM32 HAL GPIO 实现所需的私有上下文。
  */
typedef struct
{
  GPIO_TypeDef *SCLPort; /**< SCL GPIO 端口。 */
  uint16_t SCLPin;       /**< SCL GPIO 引脚掩码。 */
  GPIO_TypeDef *SDAPort; /**< SDA GPIO 端口。 */
  uint16_t SDAPin;       /**< SDA GPIO 引脚掩码。 */
} SoftI2C_STM32HALAdapterTypeDef;

SoftI2C_StatusTypeDef SoftI2C_STM32HALAdapter_Bind(
    SoftI2C_HandleTypeDef *hi2c,
    SoftI2C_STM32HALAdapterTypeDef *adapter,
    uint32_t delay_cycles,
    uint32_t clock_stretch_timeout);

#ifdef __cplusplus
}
#endif

#endif /* SOFT_I2C_STM32_HAL_ADAPTER_H */
