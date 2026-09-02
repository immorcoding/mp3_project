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

/** @brief ADR-0009 固定保留的 Flash 自检扇区。 */
typedef enum
{
    PLATFORM_FLASH_DIAGNOSTIC_REGION_HEAD = 0U, /**< ADR-0009 最低地址的 4 KiB 自检扇区。 */
    PLATFORM_FLASH_DIAGNOSTIC_REGION_TAIL       /**< ADR-0009 最高地址的 4 KiB 自检扇区。 */
} Platform_Flash_DiagnosticRegionTypeDef;

/** @brief Platform Flash 在 QSPI IRQ 中向订阅者发布的异步操作生命周期事件。 */
typedef enum
{
    PLATFORM_FLASH_OPERATION_EVENT_READ_COMPLETE = 0U, /**< 间接读取完成；普通上下文仍须收尾。 */
    PLATFORM_FLASH_OPERATION_EVENT_STATUS_MATCH,       /**< 自动状态轮询匹配，例如 WIP 清零。 */
    PLATFORM_FLASH_OPERATION_EVENT_ERROR,              /**< QSPI 或 MDMA 报错。 */
    PLATFORM_FLASH_OPERATION_EVENT_ABORTED             /**< 当前异步操作被中止。 */
} Platform_Flash_OperationEventTypeDef;

/**
 * @brief Platform Flash 在 QSPI IRQ 中调用的异步操作事件通知。
 * @note  回调运行于 IRQ 上下文；实现只能调用 xxxFromISR() 或执行其他常数时间
 *        操作。不得读取 DMA 缓冲区、调用 Platform_Flash_ProcessOperation()、
 *        记录日志或提交后续 Flash 命令。
 */
typedef void (*Platform_Flash_OperationCallback_t)(
    Platform_Flash_OperationEventTypeDef event,
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
Platform_StatusTypeDef Platform_Flash_EnableMemoryMappedMode(
    const uint8_t **mapped_base,
    uint32_t *mapped_size);
Platform_StatusTypeDef Platform_Flash_SetOperationCallback(
    Platform_Flash_OperationCallback_t operation_callback,
    void *operation_context);
Platform_StatusTypeDef Platform_Flash_ClearOperationCallback(void);
Platform_StatusTypeDef Platform_Flash_StartReadArray(
    uint32_t address,
    uint8_t *data,
    uint32_t data_length);
Platform_StatusTypeDef Platform_Flash_ProcessOperation(void);
Platform_StatusTypeDef Platform_Flash_StartDiagnosticRead(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint8_t *data,
    uint32_t data_length);
Platform_StatusTypeDef Platform_Flash_ReadDiagnostic(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint8_t *data,
    uint32_t data_length);
Platform_StatusTypeDef Platform_Flash_FillDiagnosticBuffer(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint8_t *buffer,
    uint32_t buffer_size);
Platform_StatusTypeDef Platform_Flash_StartDiagnosticErase(
    Platform_Flash_DiagnosticRegionTypeDef region);
Platform_StatusTypeDef Platform_Flash_StartDiagnosticPageProgram(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint32_t page_offset,
    const uint8_t *data,
    uint32_t data_length);
Platform_StatusTypeDef Platform_Flash_VerifyDiagnosticReadBuffer(
    Platform_Flash_DiagnosticRegionTypeDef region, const uint8_t *data, uint32_t data_length);

/** @brief 底层逻辑卷持续状态，未格式化不自动变成可用盘。 */
typedef enum
{
    PLATFORM_FLASH_VOLUME_RESET = 0,        /**< 尚未打开或已注销逻辑卷。 */
    PLATFORM_FLASH_VOLUME_READY,            /**< 卷已打开，可以受理逻辑块请求。 */
    PLATFORM_FLASH_VOLUME_BUSY,             /**< 正在执行打开、读写、同步、格式化或回收。 */
    PLATFORM_FLASH_VOLUME_UNFORMATTED,      /**< 没有可识别的 FTL 卷，不自动格式化。 */
    PLATFORM_FLASH_VOLUME_INCOMPLETE,       /**< 最新格式代次未完成 READY。 */
    PLATFORM_FLASH_VOLUME_INCOMPATIBLE,     /**< 格式、几何或版本空间不兼容。 */
    PLATFORM_FLASH_VOLUME_CORRUPT,          /**< 提交或数据校验损坏。 */
    PLATFORM_FLASH_VOLUME_ERROR             /**< 原始访问或恢复失败。 */
} Platform_Flash_VolumeStateTypeDef;

typedef struct
{
    uint32_t SectorCount;
    uint32_t SectorBytes;
    uint32_t PhysicalBytes;
} Platform_Flash_VolumeInfoTypeDef;

typedef struct
{
    uint32_t FormatVersion;
    uint64_t Epoch;
    uint32_t ValidGroups;
    uint32_t FreeBlocks;
    uint32_t StaleBlocks;
    uint32_t SessionErases;
    uint32_t DeviceError;
    uint32_t PortStatus;
} Platform_Flash_VolumeDiagnosticsTypeDef;

Platform_StatusTypeDef Platform_Flash_BindVolume(void);
Platform_StatusTypeDef Platform_Flash_OpenVolumeStart(void);
Platform_StatusTypeDef Platform_Flash_FormatVolumeStart(void);
Platform_StatusTypeDef Platform_Flash_ReadBlocksStart(uint32_t lba, uint8_t *data, uint32_t count);
Platform_StatusTypeDef Platform_Flash_WriteBlocksStart(uint32_t lba,
                                                       const uint8_t *data,
                                                       uint32_t count);
Platform_StatusTypeDef Platform_Flash_SyncVolumeStart(void);
Platform_StatusTypeDef Platform_Flash_ReclaimVolumeStart(void);
Platform_Flash_VolumeStateTypeDef Platform_Flash_GetVolumeState(void);
Platform_StatusTypeDef Platform_Flash_GetVolumeInfo(Platform_Flash_VolumeInfoTypeDef *info);
Platform_StatusTypeDef Platform_Flash_GetVolumeDiagnostics(
    Platform_Flash_VolumeDiagnosticsTypeDef *diagnostics);
bool Platform_Flash_OperationNeedsWait(void);
void Platform_Flash_AbortOperation(void);
Platform_StatusTypeDef Platform_Flash_RecoverVolume(void);

#endif /* PLATFORM_FLASH_H */
