/**
  ******************************************************************************
  * @file    st7789.h
  * @brief   ST7789 显示控制器的可复用 Device Interface。
  ******************************************************************************
  */

#ifndef ST7789_H
#define ST7789_H

#include <stdbool.h>
#include <stdint.h>

/** @brief ST7789 Device API 的立即返回状态。 */
typedef enum
{
    ST7789_OK = 0, /**< 本次操作成功。 */
    ST7789_ERROR   /**< 本次操作失败，详细原因保存在 Handle 中。 */
} ST7789_StatusTypeDef;

/** @brief ST7789 Device 的持续生命周期状态。 */
typedef enum
{
    ST7789_STATE_RESET = 0, /**< 尚未完成硬件复位。 */
    ST7789_STATE_READY,     /**< 已完成复位，可以收发命令。 */
    ST7789_STATE_BUSY,      /**< 正在执行同步串行传输。 */
    ST7789_STATE_ERROR      /**< 最近一次操作失败。 */
} ST7789_StateTypeDef;

/** @brief ST7789 Device 可理解的归一化端口状态。 */
typedef enum
{
    ST7789_PORT_OK = 0, /**< 端口操作成功。 */
    ST7789_PORT_ERROR,  /**< 未进一步分类的端口错误。 */
    ST7789_PORT_BUSY,   /**< 端口正忙。 */
    ST7789_PORT_TIMEOUT /**< 端口操作超时。 */
} ST7789_PortStatusTypeDef;

/** @brief ST7789 Device 保存的最近一次语义错误阶段。 */
typedef enum
{
    ST7789_ERROR_NONE = 0,      /**< 没有错误。 */
    ST7789_ERROR_INVALID_PARAM, /**< Handle、数据或 ID 输出参数无效。 */
    ST7789_ERROR_PORT_NOT_BOUND,/**< PortOps、Context 或必需回调未绑定。 */
    ST7789_ERROR_NOT_READY,     /**< 当前生命周期状态不允许操作。 */
    ST7789_ERROR_RESET,         /**< 硬件复位前后的端口操作失败。 */
    ST7789_ERROR_WRITE_COMMAND, /**< 写入命令字节失败。 */
    ST7789_ERROR_READ_ID        /**< 读取 RDDID 返回位流失败。 */
} ST7789_ErrorTypeDef;

/** @brief ST7789 的 24 位 RDDID 解码结果。 */
typedef struct
{
    uint8_t ID1; /**< RDDID 的第 1 个数据字节。 */
    uint8_t ID2; /**< RDDID 的第 2 个数据字节。 */
    uint8_t ID3; /**< RDDID 的第 3 个数据字节。 */
} ST7789_IDTypeDef;

/** @brief 设置 CS、D/C 或 RESET 等离散控制信号的端口函数类型。 */
typedef void (*ST7789_PortSetSignalFunc)(void *context, bool asserted);

/** @brief 设置 D/C 数据模式的端口函数类型。 */
typedef void (*ST7789_PortSetDataModeFunc)(void *context, bool data_mode);

/** @brief 同步写入一个或多个 8 位串行帧的端口函数类型。 */
typedef ST7789_PortStatusTypeDef (*ST7789_PortWriteFunc)(
    void *context,
    const uint8_t *data,
    uint32_t length);

/** @brief 同步全双工传输的端口函数类型。 */
typedef ST7789_PortStatusTypeDef (*ST7789_PortTransferFunc)(
    void *context,
    const uint8_t *transmit_data,
    uint8_t *receive_data,
    uint32_t length);

/** @brief 等待毫秒级硬件稳定时间的端口函数类型。 */
typedef void (*ST7789_PortDelayMsFunc)(void *context, uint32_t delay_ms);

/** @brief ST7789 Device 使用的串行与控制信号操作表。 */
typedef struct
{
    ST7789_PortSetSignalFunc SetChipSelect; /**< true 选中 LCD，false 释放 LCD。 */
    ST7789_PortSetDataModeFunc SetDataMode; /**< true 为数据，false 为命令。 */
    ST7789_PortSetSignalFunc SetReset;      /**< true 表示复位有效。 */
    ST7789_PortWriteFunc Write;             /**< 同步写命令或数据。 */
    ST7789_PortTransferFunc Transfer;       /**< 同步产生读时钟并接收数据。 */
    ST7789_PortDelayMsFunc DelayMs;         /**< 硬件上电、复位等待。 */
} ST7789_PortOpsTypeDef;

/** @brief ST7789 Device Handle。 */
typedef struct
{
    const ST7789_PortOpsTypeDef *PortOps; /**< 由 Adapter 安装的操作表。 */
    void *PortContext;                    /**< 与 PortOps 成对绑定的底层 Context。 */
    volatile ST7789_StateTypeDef State;   /**< 当前持续生命周期状态。 */
    volatile ST7789_ErrorTypeDef ErrorCode; /**< 最近一次 Device 层错误阶段。 */
    volatile ST7789_PortStatusTypeDef LastPortStatus; /**< 最近一次端口结果。 */
} ST7789_HandleTypeDef;

ST7789_StatusTypeDef ST7789_Init(ST7789_HandleTypeDef *hst7789);
ST7789_StatusTypeDef ST7789_ReadID(ST7789_HandleTypeDef *hst7789,
                                   ST7789_IDTypeDef *id);

#endif /* ST7789_H */
