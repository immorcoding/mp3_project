/**
  ******************************************************************************
  * @file    soft_i2c.h
  * @brief   基于 STM32 HAL GPIO 的阻塞式软件 I2C 驱动公共接口。
  *
  * @details
  *          SCL 和 SDA 必须配置为开漏输出并连接外部上拉电阻。本驱动支持：
  *          - 7 位从机地址；
  *          - 主机普通发送和接收；
  *          - 8 位或 16 位寄存器地址读写；
  *          - 时钟拉伸检测；
  *          - 初始化阶段自动总线恢复。
  *
  * @note    本驱动使用忙等待产生时序，所有 API 均为阻塞式，不应在要求严格
  *          实时性的中断中调用。
  * @warning 本模块没有仲裁丢失检测、互斥锁或 RTOS 并发保护，同一句柄在一次
  *          传输完成前不得被其他执行上下文再次调用。
  ******************************************************************************
  */

#ifndef SOFT_I2C_H
#define SOFT_I2C_H

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Exported types ------------------------------------------------------------*/
/**
  * @brief 软件 I2C API 返回状态。
  */
typedef enum
{
  SOFT_I2C_OK = 0u,  /**< 操作成功。 */
  SOFT_I2C_ERROR,    /**< 参数、NACK 或其他总线错误。 */
  SOFT_I2C_BUSY,     /**< 句柄或物理总线正忙。 */
  SOFT_I2C_TIMEOUT   /**< 等待 SCL 释放超时。 */
} SoftI2C_StatusTypeDef;

/**
  * @brief 软件 I2C 句柄状态。
  */
typedef enum
{
  SOFT_I2C_STATE_RESET = 0u, /**< 尚未成功初始化。 */
  SOFT_I2C_STATE_READY,      /**< 已初始化，可以发起传输。 */
  SOFT_I2C_STATE_BUSY,       /**< 正在执行初始化或传输。 */
  SOFT_I2C_STATE_ERROR       /**< 总线处于错误状态，需要重新初始化。 */
} SoftI2C_StateTypeDef;

/**
  * @brief 从机内部寄存器地址宽度。
  */
typedef enum
{
  SOFT_I2C_MEM_ADDR_8BIT = 1u,  /**< 发送 1 字节寄存器地址。 */
  SOFT_I2C_MEM_ADDR_16BIT = 2u  /**< 先高字节后低字节发送寄存器地址。 */
} SoftI2C_MemAddrSizeTypeDef;

/* Exported constants --------------------------------------------------------*/
/** @defgroup SOFT_I2C_Error_Code 软件 I2C 错误位
  * @brief ErrorCode 可同时记录多个错误，因此使用位掩码表示。
  * @{
  */
#define SOFT_I2C_ERROR_NONE          0x00000000u /**< 未记录错误。 */
#define SOFT_I2C_ERROR_BUS_BUSY      0x00000001u /**< 应为空闲时 SDA 仍为低。 */
#define SOFT_I2C_ERROR_NACK_ADDRESS  0x00000002u /**< 从机未应答地址字节。 */
#define SOFT_I2C_ERROR_NACK_DATA     0x00000004u /**< 从机未应答寄存器地址或数据。 */
#define SOFT_I2C_ERROR_SCL_TIMEOUT   0x00000008u /**< 释放 SCL 后超时仍未变高。 */
#define SOFT_I2C_ERROR_INVALID_PARAM 0x00000010u /**< 句柄、地址、缓冲区或长度非法。 */
/** @} */

/**
  * @brief 软件 I2C 实例句柄。
  * @note  GPIO 必须由 CubeMX 或板级代码预先配置为开漏输出。
  *        SCL/SDA 的 SET 操作只是释放开漏输出，真正的高电平依赖外部上拉。
  */
typedef struct
{
  GPIO_TypeDef *SCL_Port;              /**< SCL GPIO 端口。 */
  uint16_t SCL_Pin;                    /**< SCL GPIO 引脚掩码。 */
  GPIO_TypeDef *SDA_Port;              /**< SDA GPIO 端口。 */
  uint16_t SDA_Pin;                    /**< SDA GPIO 引脚掩码。 */
  uint32_t DelayCycles;                /**< 每个时序阶段的忙等待循环次数；不是微秒值。 */
  uint32_t ClockStretchTimeout;        /**< 等待 SCL 变高的最大轮询次数。 */
  volatile SoftI2C_StateTypeDef State; /**< 当前驱动状态。 */
  volatile uint32_t ErrorCode;         /**< 最近一次操作的 SOFT_I2C_ERROR_xxx 位集合。 */
} SoftI2C_HandleTypeDef;

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  初始化软件 I2C，并在 SDA 被拉低时自动尝试恢复总线。
  * @param  hi2c 软件 I2C 句柄指针。
  * @retval SOFT_I2C_OK      初始化成功，总线空闲。
  * @retval SOFT_I2C_ERROR   句柄参数非法。
  * @retval SOFT_I2C_BUSY    恢复后 SDA 仍被拉低。
  * @retval SOFT_I2C_TIMEOUT SCL 无法释放。
  */
SoftI2C_StatusTypeDef SoftI2C_Init(SoftI2C_HandleTypeDef *hi2c);

/**
  * @brief  轮询指定 7 位地址，检查从机是否返回 ACK。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  device_address_7bit 从机 7 位地址，不包含读写位。
  * @param  trials 最大探测次数，必须大于 0。
  * @retval SOFT_I2C_OK      从机应答。
  * @retval SOFT_I2C_ERROR   参数非法或所有探测均收到地址 NACK。
  * @retval SOFT_I2C_BUSY    句柄当前不处于 READY 状态。
  * @retval SOFT_I2C_TIMEOUT SCL 无法释放。
  */
SoftI2C_StatusTypeDef SoftI2C_IsDeviceReady(
  SoftI2C_HandleTypeDef *hi2c,
  uint8_t device_address_7bit,
  uint32_t trials);

/**
  * @brief  以主机发送模式向从机连续发送数据。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  device_address_7bit 从机 7 位地址，不包含读写位。
  * @param  data 待发送缓冲区。
  * @param  size 待发送字节数，必须大于 0。
  * @retval SoftI2C_StatusTypeDef 操作结果；详细错误见 hi2c->ErrorCode。
  * @note   地址阶段发送 (device_address_7bit << 1)，不会发送寄存器地址。
  */
SoftI2C_StatusTypeDef SoftI2C_MasterTransmit(
  SoftI2C_HandleTypeDef *hi2c,
  uint8_t device_address_7bit,
  const uint8_t *data,
  uint16_t size);

/**
  * @brief  以主机接收模式从从机连续读取数据。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  device_address_7bit 从机 7 位地址，不包含读写位。
  * @param  data 接收缓冲区。
  * @param  size 待读取字节数，必须大于 0。
  * @retval SoftI2C_StatusTypeDef 操作结果；最后一个字节后发送 NACK。
  * @note   地址阶段发送 (device_address_7bit << 1) | 1，不带寄存器语义。
  */
SoftI2C_StatusTypeDef SoftI2C_MasterReceive(
  SoftI2C_HandleTypeDef *hi2c,
  uint8_t device_address_7bit,
  uint8_t *data,
  uint16_t size);

/**
  * @brief  从从机内部寄存器连续读取数据。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  device_address_7bit 从机 7 位地址，不包含读写位。
  * @param  mem_address 从机内部寄存器地址。
  * @param  mem_address_size 寄存器地址宽度。
  * @param  data 接收缓冲区。
  * @param  size 待读取字节数，必须大于 0。
  * @retval SoftI2C_StatusTypeDef 操作结果；详细错误见 hi2c->ErrorCode。
  * @note   读取序列使用“写地址 -> 内部地址 -> 重复 START -> 读地址”。
  */
SoftI2C_StatusTypeDef SoftI2C_MemRead(
  SoftI2C_HandleTypeDef *hi2c,
  uint8_t device_address_7bit,
  uint16_t mem_address,
  SoftI2C_MemAddrSizeTypeDef mem_address_size,
  uint8_t *data,
  uint16_t size);

/**
  * @brief  向从机内部寄存器连续写入数据。
  * @param  hi2c 软件 I2C 句柄指针。
  * @param  device_address_7bit 从机 7 位地址，不包含读写位。
  * @param  mem_address 从机内部寄存器地址。
  * @param  mem_address_size 寄存器地址宽度。
  * @param  data 待发送缓冲区。
  * @param  size 待写入字节数，必须大于 0。
  * @retval SoftI2C_StatusTypeDef 操作结果；详细错误见 hi2c->ErrorCode。
  */
SoftI2C_StatusTypeDef SoftI2C_MemWrite(
  SoftI2C_HandleTypeDef *hi2c,
  uint8_t device_address_7bit,
  uint16_t mem_address,
  SoftI2C_MemAddrSizeTypeDef mem_address_size,
  const uint8_t *data,
  uint16_t size);

#ifdef __cplusplus
}
#endif

#endif
