/**
  ******************************************************************************
  * @file    platform_sd.h
  * @brief   本板唯一 SD 卡槽的公共 Platform Interface。
  *
  * @details
  *          Platform 层私有持有 SD Card Device、SDMMC1 Adapter 和 GPIO EXTI
  *          回调节点。调用者只能通过本接口初始化、刷新状态、复制信息和访问
  *          逻辑块，不能直接访问 hsd1 或修改 Device 运行状态。
  *
  *          SD 检测边沿会在 ISR 上下文调用初始化时注入的回调；SDMMC DMA 事件会调用
  *          后续由唯一订阅者通过 Platform_SD_SetTransferCallback() 设置的回调。
  *          Platform 不依赖 FreeRTOS；调用者负责把轻量事件转换为所属运行时的
  *          调度机制。当前 Storage Task 使用索引 0 处理卡检测消抖；Filesystem
  *          Service 在同一任务上下文使用索引 1 等待 DMA 完成后调用
  *          Platform_SD_CompleteTransfer()。
  ******************************************************************************
  */

#ifndef PLATFORM_SD_H
#define PLATFORM_SD_H

#include <stdbool.h>
#include <stdint.h>

#include "Platform/platform.h"

/**
  * @brief Platform 层对外公开的 SD 卡持续状态。
  */
typedef enum
{
    PLATFORM_SD_STATE_RESET = 0U,  /**< Platform SD 尚未初始化或已经反初始化。 */
    PLATFORM_SD_STATE_NOT_PRESENT, /**< 当前卡槽没有介质。 */
    PLATFORM_SD_STATE_READY,       /**< SD 卡可以进行逻辑块访问。 */
    PLATFORM_SD_STATE_BUSY,        /**< SD 卡正在执行同步或 DMA 传输。 */
    PLATFORM_SD_STATE_ERROR        /**< 最近一次底层操作失败。 */
} Platform_SD_StateTypeDef;

/**
  * @brief Platform SD 向上层报告的稳定热插拔状态变化。
  */
typedef enum
{
    PLATFORM_SD_EVENT_NONE = 0U, /**< 本次刷新没有产生介质状态变化。 */
    PLATFORM_SD_EVENT_INSERTED,  /**< SD 卡进入 READY。 */
    PLATFORM_SD_EVENT_REMOVED    /**< SD 卡进入 NOT_PRESENT。 */
} Platform_SD_EventTypeDef;

/**
  * @brief Platform 层公开的 SD 卡逻辑块信息快照。
  */
typedef struct
{
    uint64_t CapacityBytes; /**< 逻辑容量，单位为字节。 */
    uint32_t BlockCount;    /**< 可访问逻辑块总数。 */
    uint32_t BlockSize;     /**< 单个逻辑块的字节数。 */
    uint32_t CardType;      /**< HAL Adapter 提供的卡类型诊断值。 */
    uint32_t CardVersion;   /**< HAL Adapter 提供的卡版本诊断值。 */
} Platform_SD_InfoTypeDef;

/**
  * @brief Platform SD 最近一次错误的只读诊断快照。
  * @note  DeviceError 描述失败阶段，PortStatus 描述归一化后的底层结果。
  */
typedef struct
{
    uint32_t DeviceError; /**< SD Card Device 层错误阶段。 */
    uint32_t PortStatus;  /**< 归一化后的 Port 状态。 */
} Platform_SD_DiagnosticsTypeDef;

/**
  * @brief Platform SD 在 GPIO EXTI ISR 中发布卡检测边沿的回调类型。
  * @param context Platform_SD_Init() 注册时保存的调用者上下文。
  * @note  回调在 ISR 上下文执行。实现只能置位、调用 xxxFromISR() 或执行其他
  *        常数时间操作；不得记录日志、访问 SDMMC、执行文件系统操作或延时。
  */
typedef void (*Platform_SD_DetectCallback_t)(void *context);

/**
  * @brief Platform SD 在 SDMMC IRQ 中发布的 DMA 生命周期事件。
  */
typedef enum
{
    PLATFORM_SD_TRANSFER_EVENT_NONE = 0U,
    PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE,
    PLATFORM_SD_TRANSFER_EVENT_WRITE_COMPLETE,
    PLATFORM_SD_TRANSFER_EVENT_ERROR,
    PLATFORM_SD_TRANSFER_EVENT_ABORTED
} Platform_SD_TransferEventTypeDef;

/**
  * @brief Platform SD 在 SDMMC IRQ 中调用的传输事件通知。
  * @note  回调在 ISR 上下文执行；实现只能使用 xxxFromISR() 或其他常数时间操作。
  */
typedef void (*Platform_SD_TransferCallback_t)(Platform_SD_TransferEventTypeDef event,
                                                void *context);

Platform_StatusTypeDef Platform_SD_Init(Platform_SD_DetectCallback_t detect_callback,
                                        void *detect_context);
Platform_StatusTypeDef Platform_SD_SetTransferCallback(
    Platform_SD_TransferCallback_t transfer_callback,
    void *transfer_context);
Platform_StatusTypeDef Platform_SD_ClearTransferCallback(void);
Platform_StatusTypeDef Platform_SD_DeInit(void);
Platform_StatusTypeDef Platform_SD_Refresh(void);
Platform_StatusTypeDef Platform_SD_Process(Platform_SD_EventTypeDef *event);
Platform_SD_StateTypeDef Platform_SD_GetState(void);
bool Platform_SD_IsPresent(void);
Platform_StatusTypeDef Platform_SD_GetInfo(Platform_SD_InfoTypeDef *info);
Platform_StatusTypeDef Platform_SD_GetDiagnostics(Platform_SD_DiagnosticsTypeDef *diagnostics);
Platform_StatusTypeDef Platform_SD_ReadBlocks(uint8_t *data,
                                               uint32_t start_block,
                                               uint32_t block_count);
Platform_StatusTypeDef Platform_SD_WriteBlocks(const uint8_t *data,
                                                uint32_t start_block,
                                                uint32_t block_count);
Platform_StatusTypeDef Platform_SD_StartReadBlocks(uint8_t *data,
                                                    uint32_t start_block,
                                                    uint32_t block_count);
Platform_StatusTypeDef Platform_SD_StartWriteBlocks(const uint8_t *data,
                                                     uint32_t start_block,
                                                     uint32_t block_count);
Platform_StatusTypeDef Platform_SD_CompleteTransfer(
    Platform_SD_TransferEventTypeDef event);
Platform_StatusTypeDef Platform_SD_Sync(void);

#endif /* PLATFORM_SD_H */
