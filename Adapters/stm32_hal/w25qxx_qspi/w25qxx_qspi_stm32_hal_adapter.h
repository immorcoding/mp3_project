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
 * @brief Platform 注入的 STM32 HAL QSPI 后端及当前异步操作元数据。
 * @note  DMARead* 仅服务 MDMA 数组读取：IRQ 只更新完成状态，普通上下文查询
 *        状态时才执行 Cache 失效。StatusPolling* 服务页编程和扇区擦除后的
 *        `HAL_QSPI_AutoPolling_IT()`：Status Match IRQ 只更新结果，普通上下文
 *        再由 W25Qxx_Process() 推进 Device。Platform 只能初始化 Handle 和
 *        TimeoutMs，不能直接改动任一异步状态字段。
 */
typedef struct
{
    QSPI_HandleTypeDef *Handle;
    uint32_t TimeoutMs;
    uint8_t *DMAReadBuffer;
    uint32_t DMAReadByteCount;
    volatile bool DMAReadPending;
    volatile W25Qxx_BusStatusTypeDef DMAReadStatus;
    volatile bool StatusPollingPending;
    volatile W25Qxx_BusStatusTypeDef StatusPollingStatus;
    bool MemoryMappedModeEnabled;
} W25Qxx_QSPI_STM32HALAdapterTypeDef;

W25Qxx_StatusTypeDef W25Qxx_QSPI_STM32HALAdapter_Bind(
    W25Qxx_HandleTypeDef *hflash,
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter);
void W25Qxx_QSPI_STM32HALAdapter_NotifyReadComplete(
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter);
void W25Qxx_QSPI_STM32HALAdapter_NotifyStatusMatch(
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter);
void W25Qxx_QSPI_STM32HALAdapter_NotifyOperationError(
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter);
W25Qxx_BusStatusTypeDef W25Qxx_QSPI_STM32HALAdapter_EnableMemoryMappedMode(
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter,
    const W25Qxx_ArrayReadProtocolTypeDef *protocol);
W25Qxx_BusStatusTypeDef W25Qxx_QSPI_STM32HALAdapter_DisableMemoryMappedMode(
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter);

#endif /* W25QXX_QSPI_STM32_HAL_ADAPTER_H */
