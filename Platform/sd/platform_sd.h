/**
  ******************************************************************************
  * @file    platform_sd.h
  * @brief   本板唯一 SD 卡槽的公共 Platform Interface。
  *
  * @details
  *          Platform 层私有持有 SD Card Device 句柄，调用者只能通过本接口
  *          初始化、查询状态、复制信息和读写逻辑块，不能直接访问 hsd1
  *          或修改 Device 运行状态。
  *
  *          无卡是正常状态：初始化可以返回 PLATFORM_OK，同时状态为
  *          PLATFORM_SD_STATE_NOT_PRESENT。EXTI cb 只记录检测边沿；
  *          Platform_SD_Process() 在普通执行上下文完成非阻塞消抖和状态刷新。
  *          Platform_SD_Refresh() 保留为不带消抖的显式刷新入口，不得从 ISR 调用。
  ******************************************************************************
  */

#ifndef PLATFORM_SD_H
#define PLATFORM_SD_H

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

#include "Platform/platform.h"

/* Exported types ------------------------------------------------------------*/
/**
  * @brief Platform 层对外公开的 SD 卡持续状态。
  */
typedef enum
{
    PLATFORM_SD_STATE_RESET = 0u,  /**< Platform SD 尚未初始化或已经反初始化。 */
    PLATFORM_SD_STATE_NOT_PRESENT, /**< 当前卡槽没有介质。 */
    PLATFORM_SD_STATE_READY,       /**< SD 卡可以进行逻辑块访问。 */
    PLATFORM_SD_STATE_BUSY,        /**< SD 卡正在执行同步操作。 */
    PLATFORM_SD_STATE_ERROR        /**< 最近一次底层操作失败。 */
} Platform_SD_StateTypeDef;

/**
  * @brief Platform SD 向上层报告的热插拔状态变化。
  */
typedef enum
{
    PLATFORM_SD_EVENT_NONE = 0U, /**< 本次处理没有产生稳定的介质状态变化。 */
    PLATFORM_SD_EVENT_INSERTED,  /**< SD 卡经过消抖后进入 READY。 */
    PLATFORM_SD_EVENT_REMOVED    /**< SD 卡经过消抖后进入 NOT_PRESENT。 */
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
  *        HAL 原始错误仍保留在 Port 私有句柄中，不跨越 Adapter 边界。
  */
typedef struct
{
    uint32_t DeviceError;    /**< SD Card Device 层错误阶段。 */
    uint32_t PortStatus;     /**< 归一化后的 Port 状态。 */
} Platform_SD_DiagnosticsTypeDef;

/* Exported functions --------------------------------------------------------*/
Platform_StatusTypeDef Platform_SD_Init(void);
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
Platform_StatusTypeDef Platform_SD_Sync(void);

#endif /* PLATFORM_SD_H */
