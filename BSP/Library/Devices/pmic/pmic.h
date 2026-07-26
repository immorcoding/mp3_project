/**
  ******************************************************************************
  * @file    pmic.h
  * @brief   AXP2101 PMIC 设备驱动的公共接口和总线抽象。
  *
  * @details
  *          驱动通过 PMIC_BusOpsTypeDef 使用底层总线，不直接依赖软件 I2C、
  *          HAL I2C 或 GPIO。板级模块负责提供操作表和对应的 BusContext。
  ******************************************************************************
  */

#ifndef PMIC_H
#define PMIC_H

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>
#include "axp2101_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Exported constants --------------------------------------------------------*/
/** @brief AXP2101 默认 7 位 I2C 地址，不包含读写位。 */
#ifdef AXP2101_SLAVE_ADDRESS
  #define PMIC_DEFAULT_ADDRESS_7BIT   AXP2101_SLAVE_ADDRESS
#else
  #define PMIC_DEFAULT_ADDRESS_7BIT   

/** @brief PMIC_Init() 仅识别设备，不写入启动配置。 */
#define PMIC_BOOT_CONFIG_DISABLED  0u
/** @brief PMIC_Init() 识别设备后写入内置启动配置。 */
#define PMIC_BOOT_CONFIG_ENABLED   1u
/** @brief 本工程默认在 PMIC 初始化时应用内置启动配置。 */
#define PMIC_DEFAULT_BOOT_CONFIG   PMIC_BOOT_CONFIG_ENABLED

/* Exported types ------------------------------------------------------------*/
/**
  * @brief PMIC 设备驱动 API 返回状态。
  */
typedef enum
{
    PMIC_OK = 0u, /**< 操作成功。 */
    PMIC_ERROR    /**< 操作失败，详细原因见 PMIC_HandleTypeDef。 */
} PMIC_StatusTypeDef;

/**
  * @brief PMIC 层可理解的统一总线状态。
  * @note  软件 I2C、HAL I2C 等底层状态必须由板级适配器转换为此枚举。
  */
typedef enum
{
    PMIC_BUS_OK = 0u, /**< 总线操作成功。 */
    PMIC_BUS_ERROR,   /**< 未进一步分类的总线错误。 */
    PMIC_BUS_BUSY,    /**< 总线或底层句柄正忙。 */
    PMIC_BUS_TIMEOUT, /**< 总线操作超时。 */
    PMIC_BUS_NACK     /**< 从机地址或数据未应答。 */
} PMIC_BusStatusTypeDef;

/**
  * @brief  准备 PMIC 底层总线的函数类型。
  * @param  context BusContext 指向的底层总线实例。
  * @retval PMIC_BusStatusTypeDef 归一化后的总线状态。
  */
typedef PMIC_BusStatusTypeDef (*PMIC_BusPrepareFunc)(void *context);

/**
  * @brief  读取 PMIC 8 位寄存器的函数类型。
  * @param  context BusContext 指向的底层总线实例。
  * @param  device_address_7bit PMIC 7 位 I2C 地址。
  * @param  reg 起始寄存器地址。
  * @param  data 接收缓冲区。
  * @param  size 待读取字节数。
  * @retval PMIC_BusStatusTypeDef 归一化后的总线状态。
  */
typedef PMIC_BusStatusTypeDef (*PMIC_BusMemReadFunc)(void *context,
    uint8_t device_address_7bit,
    uint8_t reg,
    uint8_t *data,
    uint16_t size);

/**
  * @brief  写入 PMIC 8 位寄存器的函数类型。
  * @param  context BusContext 指向的底层总线实例。
  * @param  device_address_7bit PMIC 7 位 I2C 地址。
  * @param  reg 起始寄存器地址。
  * @param  data 待发送缓冲区。
  * @param  size 待写入字节数。
  * @retval PMIC_BusStatusTypeDef 归一化后的总线状态。
  */
typedef PMIC_BusStatusTypeDef (*PMIC_BusMemWriteFunc)(void *context,
    uint8_t device_address_7bit,
    uint8_t reg,
    const uint8_t *data,
    uint16_t size);

/**
  * @brief PMIC 总线操作表（ops表）。
  * @note  该结构体相当于 PMIC 总线接口的 vtable。所有函数必须与
  *        BusContext 指向的实际类型正确配对。
  */
typedef struct
{
    PMIC_BusPrepareFunc Prepare; /**< 准备总线；软件 I2C 初始化，硬件 I2C 可空操作。 */
    PMIC_BusMemReadFunc MemRead; /**< 从 PMIC 寄存器读取数据。 */
    PMIC_BusMemWriteFunc MemWrite; /**< 向 PMIC 寄存器写入数据。 */
} PMIC_BusOpsTypeDef;

/**
  * @brief PMIC 驱动对象的持久运行状态。
  * @note  State 保存在 Handle 中，可由调试器持续观察；Status 是某一次函数
  *        调用的立即返回值，两者用途不同。应用不应直接伪造 State。
  */
typedef enum
{
    PMIC_STATE_RESET = 0u, /**< 尚未成功初始化。 */
    PMIC_STATE_READY,      /**< 初始化完成，可以使用。 */
    PMIC_STATE_BUSY,       /**< 正在执行初始化或配置。 */
    PMIC_STATE_ERROR       /**< 最近一次操作失败。 */
} PMIC_StateTypeDef;

/**
  * @brief PMIC 驱动层错误原因。
  */
typedef enum
{
    PMIC_ERROR_NONE = 0u,      /**< 无错误。 */
    PMIC_ERROR_INVALID_PARAM,  /**< 句柄、地址或配置参数非法。 */
    PMIC_ERROR_PORT_NOT_BOUND, /**< Bus Ops 或 Bus Context 尚未绑定。 */
    PMIC_ERROR_NOT_READY,      /**< 当前生命周期状态不允许执行该操作。 */
    PMIC_ERROR_BUS_PREPARE,    /**< 底层总线准备失败。 */
    PMIC_ERROR_BUS_READ,       /**< 寄存器读取失败。 */
    PMIC_ERROR_BUS_WRITE,      /**< 寄存器写入失败。 */
    PMIC_ERROR_WRONG_CHIP_ID   /**< 读取成功，但芯片 ID 与 AXP2101 不符。 */
} PMIC_ErrorTypeDef;

/**
  * @brief PMIC 设备实例句柄。
  * @note  BusOps 和 BusContext 采用组合与依赖注入方式，使 PMIC 驱动可复用
  *        于软件 I2C、HAL I2C 或测试总线。前四个字段是依赖/配置，后四个
  *        字段是运行状态和最近一次失败的稳定语义诊断。
  */
typedef struct
{
    const PMIC_BusOpsTypeDef *BusOps; /**< 总线操作表；驱动只依赖此抽象接口。 */
    void *BusContext;                 /**< 底层总线实例，相当于操作函数的 this 指针。 */
    uint8_t Address7Bit;              /**< 7 位 I2C 地址，不包含读写位。 */
    uint8_t ApplyBootConfig;          /**< 是否在识别芯片后应用启动配置；由 Init 装载默认值。 */
    volatile PMIC_StateTypeDef State; /**< 当前驱动状态。 */
    volatile PMIC_ErrorTypeDef ErrorCode; /**< 最近一次 PMIC 层错误。 */
    volatile PMIC_BusStatusTypeDef LastBusStatus; /**< 最近一次失败对应的归一化总线状态。 */
    uint8_t LastFailedRegister;       /**< 最近一次总线失败对应的寄存器地址。 */
} PMIC_HandleTypeDef;

/* Exported functions --------------------------------------------------------*/
PMIC_StatusTypeDef PMIC_Init(PMIC_HandleTypeDef *hpmic);

PMIC_StatusTypeDef PMIC_SetALDO1Enabled(PMIC_HandleTypeDef *hpmic,
                                        bool enabled);

PMIC_StatusTypeDef PMIC_SetALDO2Enabled(PMIC_HandleTypeDef *hpmic,
                                        bool enabled);

#ifdef __cplusplus
}
#endif

#endif
