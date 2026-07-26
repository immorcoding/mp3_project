/**
  ******************************************************************************
  * @file    axp2101.h
  * @brief   AXP2101 设备驱动的公共接口和总线抽象。
  ******************************************************************************
  */

#ifndef AXP2101_H
#define AXP2101_H

#include <stdbool.h>
#include <stdint.h>

#include "Components/axp2101/axp2101_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AXP2101_DEFAULT_ADDRESS_7BIT  AXP2101_SLAVE_ADDRESS
#define AXP2101_REGISTER_FULL_MASK    0xFFu

typedef enum
{
    AXP2101_OK = 0u,
    AXP2101_ERROR
} AXP2101_StatusTypeDef;

typedef enum
{
    AXP2101_BUS_OK = 0u,
    AXP2101_BUS_ERROR,
    AXP2101_BUS_BUSY,
    AXP2101_BUS_TIMEOUT,
    AXP2101_BUS_NACK
} AXP2101_BusStatusTypeDef;

typedef AXP2101_BusStatusTypeDef (*AXP2101_BusPrepareFunc)(void *context);

typedef AXP2101_BusStatusTypeDef (*AXP2101_BusMemReadFunc)(
    void *context,
    uint8_t device_address_7bit,
    uint8_t reg,
    uint8_t *data,
    uint16_t size);

typedef AXP2101_BusStatusTypeDef (*AXP2101_BusMemWriteFunc)(
    void *context,
    uint8_t device_address_7bit,
    uint8_t reg,
    const uint8_t *data,
    uint16_t size);

typedef struct
{
    AXP2101_BusPrepareFunc Prepare;
    AXP2101_BusMemReadFunc MemRead;
    AXP2101_BusMemWriteFunc MemWrite;
} AXP2101_BusOpsTypeDef;

typedef enum
{
    AXP2101_STATE_RESET = 0u,
    AXP2101_STATE_READY,
    AXP2101_STATE_BUSY,
    AXP2101_STATE_ERROR
} AXP2101_StateTypeDef;

typedef enum
{
    AXP2101_ERROR_NONE = 0u,
    AXP2101_ERROR_INVALID_PARAM,
    AXP2101_ERROR_PORT_NOT_BOUND,
    AXP2101_ERROR_NOT_READY,
    AXP2101_ERROR_BUS_PREPARE,
    AXP2101_ERROR_BUS_READ,
    AXP2101_ERROR_BUS_WRITE,
    AXP2101_ERROR_WRONG_CHIP_ID
} AXP2101_ErrorTypeDef;

typedef struct
{
    uint8_t Register;
    uint8_t Mask;
    uint8_t Value;
} AXP2101_RegisterConfigTypeDef;

typedef struct
{
    const AXP2101_BusOpsTypeDef *BusOps;
    void *BusContext;
    uint8_t Address7Bit;
    volatile AXP2101_StateTypeDef State;
    volatile AXP2101_ErrorTypeDef ErrorCode;
    volatile AXP2101_BusStatusTypeDef LastBusStatus;
    uint8_t LastFailedRegister;
} AXP2101_HandleTypeDef;

AXP2101_StatusTypeDef AXP2101_Init(AXP2101_HandleTypeDef *haxp2101);

AXP2101_StatusTypeDef AXP2101_ApplyConfiguration(
    AXP2101_HandleTypeDef *haxp2101,
    const AXP2101_RegisterConfigTypeDef *configuration,
    uint32_t configuration_count);

AXP2101_StatusTypeDef AXP2101_SetALDO1Enabled(
    AXP2101_HandleTypeDef *haxp2101,
    bool enabled);

AXP2101_StatusTypeDef AXP2101_SetALDO2Enabled(
    AXP2101_HandleTypeDef *haxp2101,
    bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* AXP2101_H */
