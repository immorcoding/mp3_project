#ifndef AUDIO_STM32_I2S_ADAPTER_H
#define AUDIO_STM32_I2S_ADAPTER_H

#include <stdint.h>

#include "Components/audio/audio.h"
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief STM32 I2S 与硬件静音控制所需的具体上下文。
  */
typedef struct
{
    I2S_HandleTypeDef *I2SHandle; /**< CubeMX 生成并初始化的 I2S Handle。 */
    GPIO_TypeDef *MutePort;       /**< 硬件静音 GPIO 端口。 */
    uint16_t MutePin;             /**< 硬件静音 GPIO 引脚掩码。 */
} Audio_STM32I2SAdapterTypeDef;

Audio_StatusTypeDef Audio_STM32I2SAdapter_Bind(
    Audio_HandleTypeDef *haudio,
    Audio_STM32I2SAdapterTypeDef *adapter);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_STM32_I2S_ADAPTER_H */
