/**
  ******************************************************************************
  * @file    axp2101.h
  * @brief   AXP2101 设备驱动的公共接口和总线抽象。
  ******************************************************************************
  */

#ifndef AXP2101_H
#define AXP2101_H

#include <stdbool.h>
#include <stdint.h>

#include "Components/axp2101/axp2101_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief AXP2101 默认 7 位 I2C 地址，不包含最低位读写方向位。 */
#define AXP2101_DEFAULT_ADDRESS_7BIT  AXP2101_SLAVE_ADDRESS

/** @brief 寄存器配置掩码全选值，表示直接覆盖整个 8 位寄存器。 */
#define AXP2101_REGISTER_FULL_MASK    0xFFu

/**
  * @brief AXP2101 Device 函数的立即返回状态。
  * @note  返回值只表示本次调用是否完成；持续状态和详细原因保存在 Handle。
  */
typedef enum
{
    AXP2101_OK = 0u, /**< 本次操作成功。 */
    AXP2101_ERROR    /**< 本次操作失败，进一步检查 State/ErrorCode。 */
} AXP2101_StatusTypeDef;

/**
  * @brief AXP2101 Device 可理解的归一化总线结果。
  * @note  SoftI2C、HAL I2C 或测试后端必须把原始状态转换为本枚举。
  */
typedef enum
{
    AXP2101_BUS_OK = 0u, /**< 总线操作成功。 */
    AXP2101_BUS_ERROR,   /**< 未进一步分类的总线错误。 */
    AXP2101_BUS_BUSY,    /**< 总线或后端实例正忙。 */
    AXP2101_BUS_TIMEOUT, /**< 等待总线或设备响应超时。 */
    AXP2101_BUS_NACK     /**< 从机未应答地址或数据。 */
} AXP2101_BusStatusTypeDef;

/**
  * @brief 准备 AXP2101 通信后端的函数类型。
  * @param context 与 BusOps 成对绑定的后端对象。
  * @retval AXP2101_BusStatusTypeDef 归一化后的准备结果。
  */
typedef AXP2101_BusStatusTypeDef (*AXP2101_BusPrepareFunc)(void *context);

/**
  * @brief 从 AXP2101 连续寄存器读取数据的函数类型。
  * @param context 与 BusOps 成对绑定的后端对象。
  * @param device_address_7bit 不包含读写位的 7 位从机地址。
  * @param reg 起始 8 位寄存器地址。
  * @param data 接收数据的缓冲区。
  * @param size 需要读取的字节数。
  */
typedef AXP2101_BusStatusTypeDef (*AXP2101_BusMemReadFunc)(
    void *context,
    uint8_t device_address_7bit,
    uint8_t reg,
    uint8_t *data,
    uint16_t size);

/**
  * @brief 向 AXP2101 连续寄存器写入数据的函数类型。
  * @param context 与 BusOps 成对绑定的后端对象。
  * @param device_address_7bit 不包含读写位的 7 位从机地址。
  * @param reg 起始 8 位寄存器地址。
  * @param data 待发送数据缓冲区，函数不得修改其内容。
  * @param size 需要写入的字节数。
  */
typedef AXP2101_BusStatusTypeDef (*AXP2101_BusMemWriteFunc)(
    void *context,
    uint8_t device_address_7bit,
    uint8_t reg,
    const uint8_t *data,
    uint16_t size);

/**
  * @brief AXP2101 Device 所需的总线操作表。
  * @note  三个函数必须由同一个 Adapter 提供，并与 BusContext 成对绑定。
  */
typedef struct
{
    AXP2101_BusPrepareFunc Prepare;   /**< 初始化或检查当前通信后端。 */
    AXP2101_BusMemReadFunc MemRead;   /**< 从芯片连续寄存器读取数据。 */
    AXP2101_BusMemWriteFunc MemWrite; /**< 向芯片连续寄存器写入数据。 */
} AXP2101_BusOpsTypeDef;

/** @brief AXP2101 Device 的持续生命周期状态。 */
typedef enum
{
    AXP2101_STATE_RESET = 0u, /**< 尚未初始化或依赖尚未装配完成。 */
    AXP2101_STATE_READY,      /**< 芯片识别成功，可以执行寄存器和电源轨操作。 */
    AXP2101_STATE_BUSY,       /**< 正在执行总线准备、寄存器访问或配置序列。 */
    AXP2101_STATE_ERROR       /**< 最近一次关键操作失败。 */
} AXP2101_StateTypeDef;

/**
  * @brief AXP2101 Device 层最近一次失败所处的语义阶段。
  * @note  具体 BUSY、TIMEOUT 或 NACK 原因由 LastBusStatus 补充。
  */
typedef enum
{
    AXP2101_ERROR_NONE = 0u,      /**< 当前没有已记录错误。 */
    AXP2101_ERROR_INVALID_PARAM,  /**< 句柄、配置表或其他调用参数非法。 */
    AXP2101_ERROR_PORT_NOT_BOUND, /**< BusOps、必需函数或 BusContext 未绑定。 */
    AXP2101_ERROR_NOT_READY,      /**< 当前生命周期状态不允许执行该操作。 */
    AXP2101_ERROR_BUS_PREPARE,    /**< 初始化或检查通信后端失败。 */
    AXP2101_ERROR_BUS_READ,       /**< 从目标寄存器读取数据失败。 */
    AXP2101_ERROR_BUS_WRITE,      /**< 向目标寄存器写入数据失败。 */
    AXP2101_ERROR_WRONG_CHIP_ID   /**< REG03H 返回值与 AXP2101 芯片 ID 不符。 */
} AXP2101_ErrorTypeDef;

/**
  * @brief 启动配置序列中的单条 AXP2101 寄存器修改项。
  * @note  Device 使用 Mask 执行读改写；Mask 为 0xFF 时可以直接覆盖寄存器。
  */
typedef struct
{
    uint8_t Register; /**< 目标 8 位寄存器地址。 */
    uint8_t Mask;     /**< 允许修改的位，0 位保持原值。 */
    uint8_t Value;    /**< 写入值，仅 Mask 中为 1 的位生效。 */
} AXP2101_RegisterConfigTypeDef;

/**
  * @brief AXP2101 Device 实例句柄。
  * @note  BusOps 和 BusContext 由 Adapter 安装；状态与诊断字段由 Device
  *        Implementation 维护，调用者不应直接伪造。
  */
typedef struct
{
    const AXP2101_BusOpsTypeDef *BusOps; /**< 当前通信 Adapter 的操作表。 */
    void *BusContext;                    /**< 传递给 BusOps 的后端对象。 */
    uint8_t Address7Bit;                 /**< 当前使用的 7 位 I2C 从机地址。 */
    volatile AXP2101_StateTypeDef State; /**< 当前持续生命周期状态。 */
    volatile AXP2101_ErrorTypeDef ErrorCode; /**< 最近一次 Device 失败阶段。 */
    volatile AXP2101_BusStatusTypeDef LastBusStatus; /**< 最近一次归一化总线结果。 */
    uint8_t LastFailedRegister;          /**< 最近一次读写失败的寄存器地址。 */
} AXP2101_HandleTypeDef;

AXP2101_StatusTypeDef AXP2101_Init(AXP2101_HandleTypeDef *haxp2101);

AXP2101_StatusTypeDef AXP2101_ApplyConfiguration(
    AXP2101_HandleTypeDef *haxp2101,
    const AXP2101_RegisterConfigTypeDef *configuration,
    uint32_t configuration_count);

AXP2101_StatusTypeDef AXP2101_SetALDO1Enabled(
    AXP2101_HandleTypeDef *haxp2101,
    bool enabled);

AXP2101_StatusTypeDef AXP2101_SetALDO2Enabled(
    AXP2101_HandleTypeDef *haxp2101,
    bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* AXP2101_H */
