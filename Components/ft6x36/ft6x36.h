/**
  ******************************************************************************
  * @file    ft6x36.h
  * @brief   FT6X36 电容触摸控制器的 Device Interface。
  ******************************************************************************
  */

#ifndef FT6X36_H
#define FT6X36_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    FT6X36_OK = 0,
    FT6X36_ERROR
} FT6X36_StatusTypeDef;

typedef enum
{
    FT6X36_STATE_RESET = 0,
    FT6X36_STATE_READY,
    FT6X36_STATE_BUSY,
    FT6X36_STATE_ERROR
} FT6X36_StateTypeDef;

typedef enum
{
    FT6X36_PORT_OK = 0,
    FT6X36_PORT_ERROR,
    FT6X36_PORT_BUSY,
    FT6X36_PORT_TIMEOUT
} FT6X36_PortStatusTypeDef;

typedef enum
{
    FT6X36_ERROR_NONE = 0,
    FT6X36_ERROR_INVALID_PARAM,
    FT6X36_ERROR_PORT_NOT_BOUND,
    FT6X36_ERROR_INITIALIZE,
    FT6X36_ERROR_NOT_READY,
    FT6X36_ERROR_PROBE,
    FT6X36_ERROR_READ_CHIP_ID,
    FT6X36_ERROR_READ_TOUCH_POINT
} FT6X36_ErrorTypeDef;

/** @brief FT6X36 控制器报告的第一触点原始坐标。 */
typedef struct
{
    bool IsPressed;
    uint16_t X;
    uint16_t Y;
} FT6X36_RawPointTypeDef;

typedef FT6X36_PortStatusTypeDef (*FT6X36_PortInitializeFunc)(void *context);
typedef FT6X36_PortStatusTypeDef (*FT6X36_PortIsReadyFunc)(
    void *context,
    uint8_t address_7bit);
typedef FT6X36_PortStatusTypeDef (*FT6X36_PortMemReadFunc)(
    void *context,
    uint8_t address_7bit,
    uint8_t register_address,
    uint8_t *data,
    uint32_t length);

typedef struct
{
    FT6X36_PortInitializeFunc Initialize;
    FT6X36_PortIsReadyFunc IsReady;
    FT6X36_PortMemReadFunc MemRead;
} FT6X36_PortOpsTypeDef;

typedef struct
{
    const FT6X36_PortOpsTypeDef *PortOps;
    void *PortContext;
    uint8_t Address7Bit;
    volatile FT6X36_StateTypeDef State;
    volatile FT6X36_ErrorTypeDef ErrorCode;
    volatile FT6X36_PortStatusTypeDef LastPortStatus;
    volatile uint8_t LastFailedRegister;
} FT6X36_HandleTypeDef;

FT6X36_StatusTypeDef FT6X36_Init(FT6X36_HandleTypeDef *hft6x36);
FT6X36_StatusTypeDef FT6X36_ReadID(FT6X36_HandleTypeDef *hft6x36,
                                    uint8_t *chip_id);
FT6X36_StatusTypeDef FT6X36_ReadRawPoint(
    FT6X36_HandleTypeDef *hft6x36,
    FT6X36_RawPointTypeDef *point);

#endif /* FT6X36_H */
