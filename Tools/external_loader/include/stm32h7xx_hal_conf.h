/**
 * @file stm32h7xx_hal_conf.h
 * @brief 外部烧录算法使用的最小 STM32H7 HAL 配置。
 */

#ifndef STM32H7XX_HAL_CONF_H
#define STM32H7XX_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#define HSE_VALUE                 25000000UL
#define HSI_VALUE                 64000000UL
#define CSI_VALUE                 4000000UL
#define HSI48_VALUE               48000000UL
#define LSI_VALUE                 32000UL
#define LSE_VALUE                 32768UL
#define EXTERNAL_CLOCK_VALUE      12288000UL
#define VDD_VALUE                 3300UL
#define TICK_INT_PRIORITY         0U
#define USE_RTOS                  0U
#define USE_SD_TRANSCEIVER        0U
#define USE_HAL_QSPI_REGISTER_CALLBACKS 0U
#define USE_FULL_ASSERT           0U

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_MDMA_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_QSPI_MODULE_ENABLED

#include "stm32h7xx_hal_rcc.h"
#include "stm32h7xx_hal_rcc_ex.h"
#include "stm32h7xx_hal_gpio.h"
#include "stm32h7xx_hal_mdma.h"
#include "stm32h7xx_hal_cortex.h"
#include "stm32h7xx_hal_pwr.h"
#include "stm32h7xx_hal_pwr_ex.h"
#include "stm32h7xx_hal_qspi.h"

#ifdef __cplusplus
}
#endif

#endif /* STM32H7XX_HAL_CONF_H */
