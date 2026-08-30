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
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_HEAD_ERASE,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_TAIL_ERASE,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_HEAD_PROGRAM,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_TAIL_PROGRAM,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_HEAD_READBACK,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_HEAD_VERIFY,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_TAIL_READBACK,
    PLATFORM_FLASH_DIAGNOSTIC_STAGE_TAIL_VERIFY
} Platform_Flash_DiagnosticStageTypeDef;

/** @brief ADR-0009 固定保留的 Flash 自检扇区。 */
typedef enum
{
    PLATFORM_FLASH_DIAGNOSTIC_REGION_HEAD = 0U,
    PLATFORM_FLASH_DIAGNOSTIC_REGION_TAIL
} Platform_Flash_DiagnosticRegionTypeDef;

/** @brief 双 4 KiB 自检扇区的完整性结果。 */
typedef struct
{
    uint32_t TestedBytes;
    Platform_Flash_DiagnosticStageTypeDef FailedStage;
    uint32_t FailureAddress;
    uint8_t ExpectedValue;
    uint8_t ActualValue;
} Platform_Flash_DiagnosticsTypeDef;

/** @brief Platform Flash 在 QSPI IRQ 中向订阅者发布的异步读取生命周期事件。 */
typedef enum
{
    PLATFORM_FLASH_TRANSFER_EVENT_READ_COMPLETE = 0U,
    PLATFORM_FLASH_TRANSFER_EVENT_ERROR,
    PLATFORM_FLASH_TRANSFER_EVENT_ABORTED
} Platform_Flash_TransferEventTypeDef;

/**
 * @brief Platform Flash 在 QSPI IRQ 中调用的异步读取事件通知。
 * @note  回调运行于 IRQ 上下文；实现只能调用 xxxFromISR() 或执行其他常数时间
 *        操作。不得读取 DMA 缓冲区、调用 Platform_Flash_ProcessTransfer()、记录
 *        日志或提交后续 Flash 命令。
 */
typedef void (*Platform_Flash_TransferCallback_t)(
    Platform_Flash_TransferEventTypeDef event,
    void *context);

Platform_StatusTypeDef Platform_Flash_Init(void);
Platform_StatusTypeDef Platform_Flash_GetJedecID(
    Platform_Flash_JedecIDTypeDef *jedec_id);
Platform_StatusTypeDef Platform_Flash_ReadStatusRegisters(
    Platform_Flash_StatusRegistersTypeDef *status_registers);
Platform_StatusTypeDef Platform_Flash_ReadArray(
    uint32_t address,
    uint8_t *data,
    uint32_t data_length);
Platform_StatusTypeDef Platform_Flash_SetTransferCallback(
    Platform_Flash_TransferCallback_t transfer_callback,
    void *transfer_context);
Platform_StatusTypeDef Platform_Flash_ClearTransferCallback(void);
Platform_StatusTypeDef Platform_Flash_StartReadArray(
    uint32_t address,
    uint8_t *data,
    uint32_t data_length);
Platform_StatusTypeDef Platform_Flash_ProcessTransfer(void);
Platform_StatusTypeDef Platform_Flash_StartDiagnosticRead(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint8_t *data,
    uint32_t data_length);
Platform_StatusTypeDef Platform_Flash_VerifyDiagnosticReadBuffer(
    Platform_Flash_DiagnosticRegionTypeDef region,
    const uint8_t *data,
    uint32_t data_length);
Platform_StatusTypeDef Platform_Flash_RunDiagnostic(
    uint8_t *buffer,
    uint32_t buffer_size,
    Platform_Flash_DiagnosticsTypeDef *diagnostics);

#endif /* PLATFORM_FLASH_H */
