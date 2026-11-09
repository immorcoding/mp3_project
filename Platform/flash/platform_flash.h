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

/** @brief FTL 最近一次非 OK 结果的归一化分类（只用于诊断，0 表示没有失败过）。 */
typedef enum
{
    PLATFORM_FLASH_FAILURE_NONE = 0,       /**< 没有失败过。 */
    PLATFORM_FLASH_FAILURE_INVALID_PARAM,  /**< 参数、范围或几何不满足。 */
    PLATFORM_FLASH_FAILURE_NOT_READY,      /**< 卷没打开或已故障。 */
    PLATFORM_FLASH_FAILURE_NO_SPACE,       /**< 无法安全分配且没有可回收块。 */
    PLATFORM_FLASH_FAILURE_IO_ERROR,       /**< 原始访问失败或被显式中止。 */
    PLATFORM_FLASH_FAILURE_CORRUPT,        /**< 提交关联、数据、版本选择或回读验证损坏。 */
    PLATFORM_FLASH_FAILURE_INCOMPATIBLE,   /**< 格式或几何不兼容，或版本空间耗尽。 */
    PLATFORM_FLASH_FAILURE_INCOMPLETE,     /**< 最新格式代次未完成。 */
    PLATFORM_FLASH_FAILURE_UNFORMATTED,    /**< 没有可识别的卷描述。 */
    PLATFORM_FLASH_FAILURE_OTHER           /**< 其他非 OK 结果。 */
} Platform_Flash_FailureTypeDef;

/** @brief 失败发生时正在执行的 FTL 请求类别（只用于诊断，0 表示没有）。 */
typedef enum
{
    PLATFORM_FLASH_FAILED_OP_NONE = 0,     /**< 没有。 */
    PLATFORM_FLASH_FAILED_OP_OPEN,         /**< 打开卷、重建映射。 */
    PLATFORM_FLASH_FAILED_OP_FORMAT,       /**< 格式化。 */
    PLATFORM_FLASH_FAILED_OP_READ,         /**< 读逻辑扇区。 */
    PLATFORM_FLASH_FAILED_OP_WRITE,        /**< 写逻辑扇区。 */
    PLATFORM_FLASH_FAILED_OP_SYNC,         /**< 同步。 */
    PLATFORM_FLASH_FAILED_OP_RECLAIM       /**< 回收一块。 */
} Platform_Flash_FailedOperationTypeDef;

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
    /* 以下是失败诊断（#220，只读，不影响任何产品行为）：Platform 在每次 FTL 请求以非 OK 结果收尾时记下，
     * 之后的请求不会冲掉它；FTL 自己的“最近结果”会被下一次请求覆盖，所以要在 Platform 层记。
     * 目前只有后台任务读（回收失败行、Flash 压力钩子的停下行，都只在失败那一次读），不拿 Flash 卷锁：
     * 另一个任务正在收尾失败请求时，各字段之间可能差一次更新，只给人看，不用来做判断。 */
    Platform_Flash_FailureTypeDef LastFailure;               /**< 最近一次以非 OK 收尾的请求的 FTL 结果。 */
    Platform_Flash_FailedOperationTypeDef LastFailureOperation; /**< 那次请求的类别。 */
    uint32_t LastFailureRawStatus;                           /**< 失败时 FTL 最近一次原始访问的归一化结果：0 成功、1 忙、2 错误、3 超时；读不到时为 `UINT32_MAX`。 */
    uint32_t LastFailureDeviceError;                         /**< 失败时芯片后端的错误码快照。 */
    uint32_t LastFailurePortStatus;                          /**< 失败时端口状态快照。 */
    uint32_t FailureCount;                                   /**< 开机以来以非 OK 收尾的请求数（饱和在 32 位）。 */
    Platform_Flash_FailureTypeDef LastRejection;             /**< 最近一次被 FTL 在受理时就拒绝的请求的 FTL 结果（例如卷故障后的 NOT_READY）。 */
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
