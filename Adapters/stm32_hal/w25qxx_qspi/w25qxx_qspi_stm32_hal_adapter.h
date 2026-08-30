/**
  ******************************************************************************
  * @file    w25qxx_qspi_stm32_hal_adapter.h
  * @brief   STM32 HAL QSPI 到 W25Qxx BusOps 的 Adapter Interface。
  ******************************************************************************
  */

#ifndef W25QXX_QSPI_STM32_HAL_ADAPTER_H
#define W25QXX_QSPI_STM32_HAL_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

#include "Components/w25qxx/w25qxx.h"
#include "stm32h7xx_hal.h"

/**
 * @brief Platform 注入的 STM32 HAL QSPI 后端与当前 DMA 读取元数据。
 * @note  DMAReadBuffer、DMAReadByteCount、DMAReadPending 和 DMAReadStatus 仅由
 *        本 Adapter 读写：IRQ 只更新完成状态，普通上下文查询完成状态时才执行
 *        Cache 失效并清理元数据。Platform 只能初始化 Handle 和 TimeoutMs。
 */
typedef struct
{
    QSPI_HandleTypeDef *Handle;
    uint32_t TimeoutMs;
    uint8_t *DMAReadBuffer;
    uint32_t DMAReadByteCount;
    volatile bool DMAReadPending;
    volatile W25Qxx_BusStatusTypeDef DMAReadStatus;
} W25Qxx_QSPI_STM32HALAdapterTypeDef;

W25Qxx_StatusTypeDef W25Qxx_QSPI_STM32HALAdapter_Bind(
    W25Qxx_HandleTypeDef *hflash,
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter);
void W25Qxx_QSPI_STM32HALAdapter_NotifyReadComplete(
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter);
void W25Qxx_QSPI_STM32HALAdapter_NotifyReadError(
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter);

#endif /* W25QXX_QSPI_STM32_HAL_ADAPTER_H */
