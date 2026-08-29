/**
  ******************************************************************************
  * @file    w25qxx.h
  * @brief   W25Q 系列串行 NOR Flash Device 公共接口。
  *
  * @details
  *          本 Module 定义 W25Qxx 芯片协议的可移植状态和总线接缝，不包含
  *          STM32 HAL、QSPI Handle、Flash FTL 或 RTOS。具体 QSPI 后端通过
  *          W25Qxx_BusOpsTypeDef 由 Platform 装配并注入。
  ******************************************************************************
  */

#ifndef W25QXX_H
#define W25QXX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* W25Q 系列通过 0x9F Read JEDEC ID 返回的固定制造商代码。 */
#define W25QXX_MANUFACTURER_ID_WINBOND       0xEFu

/* W25Q 系列通过 0x9F Read JEDEC ID 返回的容量代码。 */
#define W25QXX_CAPACITY_ID_1MBIT             0x11u
#define W25QXX_CAPACITY_ID_2MBIT             0x12u
#define W25QXX_CAPACITY_ID_4MBIT             0x13u
#define W25QXX_CAPACITY_ID_8MBIT             0x14u
#define W25QXX_CAPACITY_ID_16MBIT            0x15u
#define W25QXX_CAPACITY_ID_32MBIT            0x16u
#define W25QXX_CAPACITY_ID_64MBIT            0x17u
#define W25QXX_CAPACITY_ID_128MBIT           0x18u
#define W25QXX_CAPACITY_ID_256MBIT           0x19u

/** @brief W25Qxx Device API 的立即返回状态。 */
typedef enum
{
    W25QXX_OK = 0,
    W25QXX_ERROR
} W25Qxx_StatusTypeDef;

/** @brief W25Qxx Device 的持续生命周期状态。 */
typedef enum
{
    W25QXX_STATE_RESET = 0,
    W25QXX_STATE_READY,
    W25QXX_STATE_BUSY,
    W25QXX_STATE_ERROR
} W25Qxx_StateTypeDef;

/** @brief W25Qxx Device 可理解的归一化底层总线状态。 */
typedef enum
{
    W25QXX_BUS_OK = 0,
    W25QXX_BUS_ERROR,
    W25QXX_BUS_BUSY,
    W25QXX_BUS_TIMEOUT
} W25Qxx_BusStatusTypeDef;

/** @brief W25Qxx Device 记录的最近一次语义失败阶段。 */
typedef enum
{
    W25QXX_ERROR_NONE = 0,
    W25QXX_ERROR_INVALID_PARAM,
    W25QXX_ERROR_BUS_NOT_BOUND,
    W25QXX_ERROR_NOT_READY,
    W25QXX_ERROR_READ_JEDEC_ID,
    W25QXX_ERROR_CHIP_MISMATCH
} W25Qxx_ErrorTypeDef;

/** @brief JEDEC Read ID 命令返回的三字节芯片标识。 */
typedef struct
{
    uint8_t ManufacturerID;
    uint8_t MemoryType;
    uint8_t CapacityID;
} W25Qxx_JedecIDTypeDef;

/**
 * @brief 当前 Device 实例允许的 JEDEC 厂商和容量组合。
 * @note  MemoryType 故意不属于此结构。相同容量的 W25Q 器件可能返回不同的
 *        MemoryType；本 Component 仍完整缓存该字节以供诊断。
 */
typedef struct
{
    uint8_t ManufacturerID;
    uint8_t CapacityID;
} W25Qxx_ExpectedJedecIDTypeDef;

/**
 * @brief 以单字节命令读取 W25Qxx 返回数据的总线函数类型。
 * @note  Instruction 的具体协议含义由 W25Qxx Device 决定；Adapter 只负责把
 *        它转换为当前总线事务。首版仅使用无地址的 JEDEC ID 读取。
 */
typedef W25Qxx_BusStatusTypeDef (*W25Qxx_BusReadCommandFunc)(
    void *context,
    uint8_t instruction,
    uint8_t *data,
    uint32_t data_length);

/** @brief W25Qxx Device 所拥有的最小串行总线操作表。 */
typedef struct
{
    W25Qxx_BusReadCommandFunc ReadCommand;
} W25Qxx_BusOpsTypeDef;

/** @brief W25Qxx Device 实例句柄。 */
typedef struct
{
    const W25Qxx_BusOpsTypeDef *BusOps;
    void *BusContext;
    volatile W25Qxx_StateTypeDef State;
    volatile W25Qxx_ErrorTypeDef ErrorCode;
    volatile W25Qxx_BusStatusTypeDef LastBusStatus;
    const W25Qxx_ExpectedJedecIDTypeDef *ExpectedJedecID;
    W25Qxx_JedecIDTypeDef JedecID;
} W25Qxx_HandleTypeDef;

W25Qxx_StatusTypeDef W25Qxx_Init(W25Qxx_HandleTypeDef *hflash);
W25Qxx_StatusTypeDef W25Qxx_GetJedecID(
    W25Qxx_HandleTypeDef *hflash,
    W25Qxx_JedecIDTypeDef *jedec_id);

#ifdef __cplusplus
}
#endif

#endif /* W25QXX_H */
