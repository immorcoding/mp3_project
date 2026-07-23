/**
  ******************************************************************************
  * @file    board_sd.h
  * @brief   本板唯一 SD 卡槽的公共 Board Interface。
  *
  * @details
  *          Board 层私有持有 SD Card Device 句柄，调用者只能通过本接口
  *          初始化、查询状态、复制信息和读写逻辑块，不能直接访问 hsd1
  *          或修改 Device 运行状态。
  ******************************************************************************
  */

#ifndef BOARD_SD_H
#define BOARD_SD_H

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

#include "BSP/Board/board.h"

/* Exported types ------------------------------------------------------------*/
/**
  * @brief Board 层对外公开的 SD 卡持续状态。
  */
typedef enum
{
    BOARD_SD_STATE_RESET = 0u,  /**< Board SD 尚未初始化或已经反初始化。 */
    BOARD_SD_STATE_NOT_PRESENT, /**< 当前卡槽没有介质。 */
    BOARD_SD_STATE_READY,       /**< SD 卡可以进行逻辑块访问。 */
    BOARD_SD_STATE_BUSY,        /**< SD 卡正在执行同步操作。 */
    BOARD_SD_STATE_ERROR        /**< 最近一次底层操作失败。 */
} Board_SD_StateTypeDef;

/**
  * @brief Board 层公开的 SD 卡逻辑块信息快照。
  */
typedef struct
{
    uint64_t CapacityBytes; /**< 逻辑容量，单位为字节。 */
    uint32_t BlockCount;    /**< 可访问逻辑块总数。 */
    uint32_t BlockSize;     /**< 单个逻辑块的字节数。 */
    uint32_t CardType;      /**< HAL Adapter 提供的卡类型诊断值。 */
    uint32_t CardVersion;   /**< HAL Adapter 提供的卡版本诊断值。 */
} Board_SD_InfoTypeDef;

/**
  * @brief Board SD 最近一次错误的只读诊断快照。
  * @note  DeviceError 描述失败阶段，PortStatus 描述归一化后的底层结果。
  *        HAL 原始错误仍保留在 Port 私有句柄中，不跨越 Adapter 边界。
  */
typedef struct
{
    uint32_t DeviceError;    /**< SD Card Device 层错误阶段。 */
    uint32_t PortStatus;     /**< 归一化后的 Port 状态。 */
} Board_SD_DiagnosticsTypeDef;

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  绑定本板 SDMMC Adapter 并初始化当前介质。
  * @retval BOARD_OK 状态同步成功；无卡时状态为 NOT_PRESENT。
  * @retval BOARD_SD_ERROR Adapter 绑定或介质初始化失败。
  */
Board_StatusTypeDef Board_SD_Init(void);

/** @brief 反初始化本板 SD 卡槽。 */
Board_StatusTypeDef Board_SD_DeInit(void);

/**
  * @brief  根据稳定后的 SD_CD 电平刷新插拔状态。
  * @note   不提供去抖，且不得在 EXTI ISR 中调用。
  */
Board_StatusTypeDef Board_SD_Refresh(void);

/** @brief 获取本板 SD 卡槽的持续状态。 */
Board_SD_StateTypeDef Board_SD_GetState(void);

/** @brief 读取当前低有效 SD_CD 电平。 */
bool Board_SD_IsPresent(void);

/** @brief 将当前介质信息复制到调用者提供的对象。 */
Board_StatusTypeDef Board_SD_GetInfo(Board_SD_InfoTypeDef *info);

/** @brief 复制最近一次 Device 和 Port 错误快照。 */
Board_StatusTypeDef Board_SD_GetDiagnostics(Board_SD_DiagnosticsTypeDef *diagnostics);

/** @brief 从本板 SD 卡读取连续逻辑块。 */
Board_StatusTypeDef Board_SD_ReadBlocks(uint8_t *data,
                                        uint32_t start_block,
                                        uint32_t block_count);

/** @brief 向本板 SD 卡写入连续逻辑块。 */
Board_StatusTypeDef Board_SD_WriteBlocks(const uint8_t *data,
                                         uint32_t start_block,
                                         uint32_t block_count);

/** @brief 等待本板 SD 卡完成内部操作。 */
Board_StatusTypeDef Board_SD_Sync(void);

#endif /* BOARD_SD_H */
