/**
  ******************************************************************************
  * @file    w25qxx_qspi_stm32_hal_adapter.h
  * @brief   STM32 HAL QSPI 到 W25Qxx BusOps 的 Adapter Interface。
  ******************************************************************************
  */

#ifndef W25QXX_QSPI_STM32_HAL_ADAPTER_H
#define W25QXX_QSPI_STM32_HAL_ADAPTER_H

#include <stdint.h>

#include "Components/w25qxx/w25qxx.h"
#include "stm32h7xx_hal.h"

/** @brief Platform 注入的 STM32 HAL QSPI 后端与同步事务参数。 */
typedef struct
{
    QSPI_HandleTypeDef *Handle;
    uint32_t TimeoutMs;
} W25Qxx_QSPI_STM32HALAdapterTypeDef;

W25Qxx_StatusTypeDef W25Qxx_QSPI_STM32HALAdapter_Bind(
    W25Qxx_HandleTypeDef *hflash,
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter);

#endif /* W25QXX_QSPI_STM32_HAL_ADAPTER_H */
