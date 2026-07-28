/**
  ******************************************************************************
  * @file    sd_stm32_hal_adapter.h
  * @brief   STM32 HAL SDMMC 和 GPIO 到 SD Card Port Interface 的 Adapter。
  *
  * @details
  *          Bind 只把当前 STM32H743 SDMMC Adapter Ops 与 Platform 提供的
  *          Context 成对安装到 Device Handle，不固定使用 hsd1 或某个卡
  *          检测引脚，也不在绑定阶段初始化控制器或产生总线波形。
  *          当前初始化实现仍固定配置 SDMMC1；扩展 SDMMC2 时应把外设实例
  *          和初始化参数进一步放入 Context。
  ******************************************************************************
  */

#ifndef SD_STM32_HAL_ADAPTER_H
#define SD_STM32_HAL_ADAPTER_H

#include "Components/sd/sd.h"
#include "stm32h7xx_hal.h"

/**
  * @brief STM32 HAL SDMMC 后端及卡检测输入所需的具体上下文。
  * @note  对象由 Platform 长期持有；Handle 和 GPIO 指针只是对 CubeMX/Vendor
  *        实例的借用引用，Adapter Context 不拥有这些对象的生命周期。
  */
typedef struct
{
    SD_HandleTypeDef *Handle;   /**< CubeMX 生成的具体 SDMMC HAL Handle。 */
    GPIO_TypeDef *DetectPort;   /**< 卡检测输入所在的 GPIO 端口。 */
    uint16_t DetectPin;         /**< 卡检测输入对应的 GPIO_Pin 位掩码。 */
    GPIO_PinState PresentState; /**< 表示“已插卡”的有效电平。 */
} SDCard_STM32HALAdapterTypeDef;

SDCard_StatusTypeDef SDCard_STM32HALAdapter_Bind(
    SDCard_HandleTypeDef *hsdcard,
    SDCard_STM32HALAdapterTypeDef *adapter);

#endif /* SD_STM32_HAL_ADAPTER_H */
