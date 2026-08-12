/**
  ******************************************************************************
  * @file    audio_i2s_stm32_hal_adapter.h
  * @brief   STM32 HAL I2S/GPIO Audio Adapter 的 Context 和绑定接口。
  ******************************************************************************
  */

#ifndef AUDIO_I2S_STM32_HAL_ADAPTER_H
#define AUDIO_I2S_STM32_HAL_ADAPTER_H

#include <stdint.h>

#include "Components/audio/audio.h"
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief STM32 I2S 与硬件静音控制所需的具体上下文。
  * @note  对象由 Platform 长期持有；I2S Handle 和 GPIO Port 只是对
  *        CubeMX/Vendor 对象的借用引用，不由本 Context 创建或销毁。
  */
typedef struct
{
    I2S_HandleTypeDef *I2SHandle; /**< CubeMX 生成并初始化的 I2S Handle。 */
    GPIO_TypeDef *MutePort;       /**< 硬件静音 GPIO 端口。 */
    uint16_t MutePin;             /**< 硬件静音 GPIO 引脚掩码。 */
} AudioI2S_STM32HALAdapterTypeDef;

Audio_StatusTypeDef AudioI2S_STM32HALAdapter_Bind(
    Audio_HandleTypeDef *haudio,
    AudioI2S_STM32HALAdapterTypeDef *adapter);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_I2S_STM32_HAL_ADAPTER_H */
