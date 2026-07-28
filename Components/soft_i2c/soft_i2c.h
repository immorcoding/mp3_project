/**
  ******************************************************************************
  * @file    soft_i2c.h
  * @brief   与具体 MCU GPIO 实现无关的阻塞式软件 I2C 组件公共接口。
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
  * @note    设备地址统一使用不包含读写位的 7 位形式；寄存器读取采用
  *          “写内部地址、重复 START、读数据”的标准组合传输。
  * @warning 本模块没有仲裁丢失检测、互斥锁或 RTOS 并发保护，同一句柄在一次
  *          传输完成前不得被其他执行上下文再次调用。
  ******************************************************************************
  */

#ifndef SOFT_I2C_H
#define SOFT_I2C_H

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

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

/**
  * @brief 软件 I2C 算法能够操作的逻辑总线线条。
  */
typedef enum
{
  SOFT_I2C_LINE_SCL = 0u, /**< 时钟线。 */
  SOFT_I2C_LINE_SDA       /**< 数据线。 */
} SoftI2C_LineTypeDef;

/**
  * @brief 开漏线条的逻辑驱动状态。
  * @note  RELEASED 表示停止主动拉低，由外部上拉电阻产生高电平。
  */
typedef enum
{
  SOFT_I2C_LINE_LOW = 0u, /**< 主动拉低线条。 */
  SOFT_I2C_LINE_RELEASED  /**< 释放开漏线条。 */
} SoftI2C_LineStateTypeDef;

/**
  * @brief 驱动或释放一条逻辑 I2C 线的 GPIO 函数类型。
  * @param context 与 GPIOOps 成对绑定的具体 GPIO 后端对象。
  * @param line 需要操作的 SCL 或 SDA。
  * @param state 主动拉低或释放开漏输出。
  */
typedef void (*SoftI2C_GPIOWriteFunc)(void *context,
                                      SoftI2C_LineTypeDef line,
                                      SoftI2C_LineStateTypeDef state);

/**
  * @brief 读取一条逻辑 I2C 线实际电平的 GPIO 函数类型。
  * @param context 与 GPIOOps 成对绑定的只读 GPIO 后端对象。
  * @param line 需要采样的 SCL 或 SDA。
  * @retval true 线条实际为高电平。
  * @retval false 线条实际为低电平。
  */
typedef bool (*SoftI2C_GPIOReadFunc)(const void *context,
                                     SoftI2C_LineTypeDef line);

/**
  * @brief 软件 I2C 组件依赖的最小 GPIO 操作表。
  * @note  接口由组件拥有，具体 MCU Adapter 负责实现。
  */
typedef struct
{
  SoftI2C_GPIOWriteFunc Write; /**< 驱动或释放指定线条。 */
  SoftI2C_GPIOReadFunc Read;   /**< 读取指定线条的实际电平。 */
} SoftI2C_GPIOOpsTypeDef;

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
  * @note  GPIO 必须由 Adapter 或板级代码预先配置为开漏输出。
  *        SCL/SDA 的 SET 操作只是释放开漏输出，真正的高电平依赖外部上拉。
  */
typedef struct
{
  const SoftI2C_GPIOOpsTypeDef *GPIOOps; /**< GPIO Adapter 操作表。 */
  void *GPIOContext;                    /**< 传递给 GPIO Adapter 的私有上下文。 */
  uint32_t DelayCycles;                /**< 每个时序阶段的忙等待循环次数；不是微秒值。 */
  uint32_t ClockStretchTimeout;        /**< 等待 SCL 变高的最大轮询次数。 */
  volatile SoftI2C_StateTypeDef State; /**< 当前驱动状态。 */
  volatile uint32_t ErrorCode;         /**< 最近一次操作的 SOFT_I2C_ERROR_xxx 位集合。 */
} SoftI2C_HandleTypeDef;

/* Exported functions --------------------------------------------------------*/
SoftI2C_StatusTypeDef SoftI2C_Init(SoftI2C_HandleTypeDef *hi2c);

SoftI2C_StatusTypeDef SoftI2C_IsDeviceReady(SoftI2C_HandleTypeDef *hi2c,
                                            uint8_t device_address_7bit,
                                            uint32_t trials);

SoftI2C_StatusTypeDef SoftI2C_MasterTransmit(SoftI2C_HandleTypeDef *hi2c,
                                             uint8_t device_address_7bit,
                                             const uint8_t *data,
                                             uint16_t size);

SoftI2C_StatusTypeDef SoftI2C_MasterReceive(SoftI2C_HandleTypeDef *hi2c,
                                            uint8_t device_address_7bit,
                                            uint8_t *data,
                                            uint16_t size);

SoftI2C_StatusTypeDef SoftI2C_MemRead(SoftI2C_HandleTypeDef *hi2c,
                                      uint8_t device_address_7bit,
                                      uint16_t mem_address,
                                      SoftI2C_MemAddrSizeTypeDef mem_address_size,
                                      uint8_t *data,
                                      uint16_t size);

SoftI2C_StatusTypeDef SoftI2C_MemWrite(SoftI2C_HandleTypeDef *hi2c,
                                       uint8_t device_address_7bit,
                                       uint16_t mem_address,
                                       SoftI2C_MemAddrSizeTypeDef mem_address_size,
                                       const uint8_t *data,
                                       uint16_t size);

#ifdef __cplusplus
}
#endif

#endif
