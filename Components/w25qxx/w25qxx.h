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

#include <stdbool.h>
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

/* W25Qxx Quad Input Page Program 的单页最大数据量，单位为字节。 */
#define W25QXX_PAGE_PROGRAM_MAX_SIZE_BYTES   256u

/* W25Q256 4-byte Sector Erase 的固定扇区粒度，单位为字节。 */
#define W25QXX_SECTOR_ERASE_SIZE_BYTES       (4u * 1024u)

/* 当前 W25Q256 的 0xEC Quad I/O Read 起始地址最低对齐要求，单位为字节。 */
#define W25QXX_QUAD_READ_ADDRESS_ALIGNMENT_BYTES  4u

/** @brief W25Qxx Device API 的立即返回状态。 */
typedef enum
{
    W25QXX_OK = 0,
    W25QXX_BUSY,
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
    W25QXX_ERROR_FLASH_BUSY,
    W25QXX_ERROR_READ_JEDEC_ID,
    W25QXX_ERROR_CHIP_MISMATCH,
    W25QXX_ERROR_READ_SFDP,
    W25QXX_ERROR_INVALID_SFDP_SIGNATURE,
    W25QXX_ERROR_READ_STATUS_REGISTER_1,
    W25QXX_ERROR_READ_STATUS_REGISTER_2,
    W25QXX_ERROR_WRITE_ENABLE,
    W25QXX_ERROR_WRITE_ENABLE_NOT_LATCHED,
    W25QXX_ERROR_WRITE_STATUS_REGISTER_2,
    W25QXX_ERROR_STATUS_REGISTER_WRITE_TIMEOUT,
    W25QXX_ERROR_QUAD_NOT_ENABLED,
    W25QXX_ERROR_UNSUPPORTED_ARRAY_OPERATION,
    W25QXX_ERROR_INVALID_ADDRESS,
    W25QXX_ERROR_INVALID_READ_ADDRESS_ALIGNMENT,
    W25QXX_ERROR_INVALID_DATA_LENGTH,
    W25QXX_ERROR_PAGE_BOUNDARY,
    W25QXX_ERROR_SECTOR_BOUNDARY,
    W25QXX_ERROR_READ_ARRAY,
    W25QXX_ERROR_READ_ARRAY_TIMEOUT,
    W25QXX_ERROR_PROGRAM_PAGE,
    W25QXX_ERROR_PAGE_PROGRAM_TIMEOUT,
    W25QXX_ERROR_ERASE_SECTOR,
    W25QXX_ERROR_SECTOR_ERASE_TIMEOUT
} W25Qxx_ErrorTypeDef;

/** @brief W25Qxx 间接事务一个阶段使用的数据线数量。 */
typedef enum
{
    W25QXX_BUS_LINES_1 = 1,
    W25QXX_BUS_LINES_2 = 2,
    W25QXX_BUS_LINES_4 = 4
} W25Qxx_BusLineModeTypeDef;

/**
 * @brief 一条带地址 W25Qxx 间接事务的可移植阶段配置。
 * @note  AddressLineMode 描述地址阶段，DataLineMode 描述数据阶段；两者可以
 *        不同。HasAlternateByte 为 true 时，Adapter 必须在地址和 dummy 周期
 *        之间发送一个 8-bit Alternate Byte。W25Q256 的 0xEC 使用该字段发送
 *        连续读取模式字节 0xFF；SFDP 和 0x34 页编程均不使用它。
 */
typedef struct
{
    uint8_t AddressLength;
    W25Qxx_BusLineModeTypeDef AddressLineMode;
    W25Qxx_BusLineModeTypeDef DataLineMode;
    bool HasAlternateByte;
    uint8_t AlternateByte;
    W25Qxx_BusLineModeTypeDef AlternateByteLineMode;
    uint8_t DummyCycles;
} W25Qxx_BusAddressedTransferConfigTypeDef;

/** @brief W25Qxx Device 当前由 Component 管理的异步原始操作。 */
typedef enum
{
    W25QXX_OPERATION_NONE = 0,
    W25QXX_OPERATION_ARRAY_READ,
    W25QXX_OPERATION_PAGE_PROGRAM,
    W25QXX_OPERATION_SECTOR_ERASE
} W25Qxx_OperationTypeDef;

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
 * @brief W25Qxx 状态寄存器的原始值及其当前可用位语义。
 * @note  IsWriteInProgress、IsWriteEnabled 和 IsQuadEnabled 分别对应当前器件
 *        的 SR1.WIP、SR1.WEL 和 SR2.QE。原始寄存器字节同时保留，以免公开接口
 *        丢失后续扩展所需的芯片信息。
 */
typedef struct
{
    uint8_t StatusRegister1;
    uint8_t StatusRegister2;
    bool IsWriteInProgress;
    bool IsWriteEnabled;
    bool IsQuadEnabled;
} W25Qxx_StatusRegistersTypeDef;

/**
 * @brief 以单字节命令读取 W25Qxx 返回数据的总线函数类型。
 * @note  Instruction 的具体协议含义由 W25Qxx Device 决定；Adapter 只负责把
 *        它转换为当前总线事务。首版用于无地址的 JEDEC ID 读取。
 */
typedef W25Qxx_BusStatusTypeDef (*W25Qxx_BusReadCommandFunc)(
    void *context,
    uint8_t instruction,
    uint8_t *data,
    uint32_t data_length);

/**
 * @brief 以单字节命令读取 W25Qxx 中带地址返回数据的总线函数类型。
 * @note  transfer_config 由 W25Qxx Device 按具体命令填写；Adapter 只把其中的
 *        地址、交替字节、dummy cycle 和数据阶段映射为当前 QSPI 外设参数。首版
 *        用于 SFDP 的 24-bit 单线读取和 W25Q256 0xEC 的 32-bit Quad I/O 读取。
 */
typedef W25Qxx_BusStatusTypeDef (*W25Qxx_BusReadAddressedCommandFunc)(
    void *context,
    uint8_t instruction,
    uint32_t address,
    const W25Qxx_BusAddressedTransferConfigTypeDef *transfer_config,
    uint8_t *data,
    uint32_t data_length);

/**
 * @brief 启动一条由 W25Qxx 描述的非阻塞带地址读取命令的总线函数类型。
 * @note  Device 决定命令、地址和传输阶段；Adapter 只开始底层数据搬运并立即
 *        返回。完成结果必须由 GetReadAddressedCommandStatus 在普通上下文读取，
 *        因而该接缝可由 STM32 MDMA、其他 DMA 或等价异步机制实现，而不向
 *        Component 暴露 HAL 或中断细节。
 */
typedef W25Qxx_BusStatusTypeDef (*W25Qxx_BusStartReadAddressedCommandFunc)(
    void *context,
    uint8_t instruction,
    uint32_t address,
    const W25Qxx_BusAddressedTransferConfigTypeDef *transfer_config,
    uint8_t *data,
    uint32_t data_length);

/**
 * @brief 查询当前非阻塞带地址读取命令的完成状态的总线函数类型。
 * @note  返回 BUSY 表示尚未收到下层完成或错误事件；返回 OK 时 Adapter 必须已
 *        完成其读取后 Cache 收尾，data 才可由 CPU 消费。该函数只可由任务等
 *        普通上下文调用，ISR 只负责更新 Adapter 私有完成状态。
 */
typedef W25Qxx_BusStatusTypeDef (*W25Qxx_BusGetReadAddressedCommandStatusFunc)(
    void *context);

/**
 * @brief 以单字节无数据命令控制 W25Qxx 的总线函数类型。
 * @note  Instruction 的协议语义仍由 W25Qxx Device 决定；Adapter 只负责发送
 *        不带地址和数据阶段的当前总线事务。首版用于 Write Enable（0x06）。
 */
typedef W25Qxx_BusStatusTypeDef (*W25Qxx_BusExecuteCommandFunc)(
    void *context,
    uint8_t instruction);

/**
 * @brief 以单字节命令控制 W25Qxx 的带地址、无数据事务的总线函数类型。
 * @note  Device 决定地址长度、地址线数和指令语义；Adapter 只映射事务。首版
 *        用于 W25Q256 0x21 的 32-bit 单线 4 KiB Sector Erase。
 */
typedef W25Qxx_BusStatusTypeDef (*W25Qxx_BusExecuteAddressedCommandFunc)(
    void *context,
    uint8_t instruction,
    uint32_t address,
    const W25Qxx_BusAddressedTransferConfigTypeDef *transfer_config);

/**
 * @brief 以单字节无地址命令向 W25Qxx 写入数据的总线函数类型。
 * @note  Device 决定 Instruction 与数据长度；Adapter 只映射为当前总线事务。
 *        首版用于 Write Status Register-2（0x31）的单字节 SR2 写入。
 */
typedef W25Qxx_BusStatusTypeDef (*W25Qxx_BusWriteCommandFunc)(
    void *context,
    uint8_t instruction,
    const uint8_t *data,
    uint32_t data_length);

/**
 * @brief 以单字节命令向 W25Qxx 指定地址写入数据的总线函数类型。
 * @note  transfer_config 由 Device 决定地址长度、各阶段线数和 dummy 语义；
 *        Adapter 只映射事务。首版用于 W25Q256 0x34 的 32-bit 地址、单线地址
 *        和四线页数据输入。
 */
typedef W25Qxx_BusStatusTypeDef (*W25Qxx_BusWriteAddressedCommandFunc)(
    void *context,
    uint8_t instruction,
    uint32_t address,
    const W25Qxx_BusAddressedTransferConfigTypeDef *transfer_config,
    const uint8_t *data,
    uint32_t data_length);

/**
 * @brief 取得单调递增毫秒计数的总线时间源函数类型。
 * @note  该时间源只用于 Component 内部的有界启动期状态轮询；其溢出由无符号
 *        差值计算处理。它不构成通用延时 Interface，也不负责 RTOS 调度。
 */
typedef uint32_t (*W25Qxx_BusGetTickMsFunc)(void *context);

/** @brief W25Qxx Device 所拥有的最小串行总线操作表。 */
typedef struct
{
    W25Qxx_BusReadCommandFunc ReadCommand;
    W25Qxx_BusReadAddressedCommandFunc ReadAddressedCommand;
    W25Qxx_BusStartReadAddressedCommandFunc StartReadAddressedCommand;
    W25Qxx_BusGetReadAddressedCommandStatusFunc GetReadAddressedCommandStatus;
    W25Qxx_BusExecuteCommandFunc ExecuteCommand;
    W25Qxx_BusExecuteAddressedCommandFunc ExecuteAddressedCommand;
    W25Qxx_BusWriteCommandFunc WriteCommand;
    W25Qxx_BusWriteAddressedCommandFunc WriteAddressedCommand;
    W25Qxx_BusGetTickMsFunc GetTickMs;
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
    volatile W25Qxx_OperationTypeDef ActiveOperation;
    volatile uint32_t ActiveOperationStartTickMs;
} W25Qxx_HandleTypeDef;

W25Qxx_StatusTypeDef W25Qxx_Init(W25Qxx_HandleTypeDef *hflash);
W25Qxx_StatusTypeDef W25Qxx_GetJedecID(
    W25Qxx_HandleTypeDef *hflash,
    W25Qxx_JedecIDTypeDef *jedec_id);
W25Qxx_StatusTypeDef W25Qxx_ProbeSFDP(W25Qxx_HandleTypeDef *hflash);
W25Qxx_StatusTypeDef W25Qxx_ReadStatusRegisters(
    W25Qxx_HandleTypeDef *hflash,
    W25Qxx_StatusRegistersTypeDef *status_registers);
W25Qxx_StatusTypeDef W25Qxx_EnsureQuadEnabled(
    W25Qxx_HandleTypeDef *hflash);
W25Qxx_StatusTypeDef W25Qxx_Read(
    W25Qxx_HandleTypeDef *hflash,
    uint32_t address,
    uint8_t *data,
    uint32_t data_length);
W25Qxx_StatusTypeDef W25Qxx_StartRead(
    W25Qxx_HandleTypeDef *hflash,
    uint32_t address,
    uint8_t *data,
    uint32_t data_length);
W25Qxx_StatusTypeDef W25Qxx_ProgramPageStart(
    W25Qxx_HandleTypeDef *hflash,
    uint32_t address,
    const uint8_t *data,
    uint32_t data_length);
W25Qxx_StatusTypeDef W25Qxx_SectorEraseStart(
    W25Qxx_HandleTypeDef *hflash,
    uint32_t address);
W25Qxx_StatusTypeDef W25Qxx_Process(W25Qxx_HandleTypeDef *hflash);

#ifdef __cplusplus
}
#endif

#endif /* W25QXX_H */
