/**
  ******************************************************************************
  * @file    platform_flash.h
  * @brief   当前 PCB W25Q256 外部 NOR Flash 的 Platform Interface。
  ******************************************************************************
  */

#ifndef PLATFORM_FLASH_H
#define PLATFORM_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "Platform/platform.h"

/** @brief 当前 PCB W25Q256 返回的 JEDEC 三字节芯片标识。 */
typedef struct
{
    uint8_t ManufacturerID;
    uint8_t MemoryType;
    uint8_t CapacityID;
} Platform_Flash_JedecIDTypeDef;

/** @brief 当前 PCB W25Q256 的实时状态寄存器快照。 */
typedef struct
{
    uint8_t StatusRegister1;
    uint8_t StatusRegister2;
    bool IsWriteInProgress;
    bool IsWriteEnabled;
    bool IsQuadEnabled;
} Platform_Flash_StatusRegistersTypeDef;

/** @brief 双 4 KiB 自检扇区诊断中的首个失败阶段。 */
typedef enum
{
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_NONE = 0,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_CYCLE_COUNTER,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_HEAD_ERASE,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_TAIL_ERASE,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_HEAD_PROGRAM,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_TAIL_PROGRAM,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_HEAD_READBACK,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_HEAD_VERIFY,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_TAIL_READBACK,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_TAIL_VERIFY
} Platform_Flash_DiagnosticStageTypeDef;

/** @brief 双 4 KiB 自检扇区的完整性与端到端时序结果。 */
typedef struct
{
    uint32_t TestedBytes;
    Platform_Flash_DiagnosticStageTypeDef FailedStage;
    uint32_t FailureAddress;
    uint8_t ExpectedValue;
    uint8_t ActualValue;
    uint32_t EraseElapsedMilliseconds;
    uint32_t ProgramElapsedMilliseconds;
    uint32_t ReadElapsedMilliseconds;
    uint32_t ProgramSpeedMiBPerSecondX100;
    uint32_t ReadSpeedMiBPerSecondX100;
} Platform_Flash_DiagnosticsTypeDef;

Platform_StatusTypeDef Platform_Flash_Init(void);
Platform_StatusTypeDef Platform_Flash_GetJedecID(
    Platform_Flash_JedecIDTypeDef *jedec_id);
Platform_StatusTypeDef Platform_Flash_ReadStatusRegisters(
    Platform_Flash_StatusRegistersTypeDef *status_registers);
Platform_StatusTypeDef Platform_Flash_ReadArray(
    uint32_t address,
    uint8_t *data,
    uint32_t data_length);
Platform_StatusTypeDef Platform_Flash_RunDiagnostic(
    uint8_t *buffer,
    uint32_t buffer_size,
    Platform_Flash_DiagnosticsTypeDef *diagnostics);

#endif /* PLATFORM_FLASH_H */
