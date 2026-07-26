/**
  ******************************************************************************
  * @file    sd_stm32_hal_adapter.h
  * @brief   本板 STM32 HAL SDMMC Adapter 的绑定接口。
  *
  * @details
  *          Bind 只把 SDMMC1 Ops 与对应的 hsd1 Context 成对安装到 Device
  *          Handle，不初始化控制器、不枚举 SD 卡，也不产生总线波形。
  ******************************************************************************
  */

#ifndef SD_STM32_HAL_ADAPTER_H
#define SD_STM32_HAL_ADAPTER_H

#include "Components/sd/sd.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    SD_HandleTypeDef *Handle;
    GPIO_TypeDef *DetectPort;
    uint16_t DetectPin;
    GPIO_PinState PresentState;
} SDCard_STM32HALAdapterTypeDef;

SDCard_StatusTypeDef SDCard_STM32HALAdapter_Bind(
    SDCard_HandleTypeDef *hsdcard,
    SDCard_STM32HALAdapterTypeDef *adapter);

#endif /* SD_STM32_HAL_ADAPTER_H */
