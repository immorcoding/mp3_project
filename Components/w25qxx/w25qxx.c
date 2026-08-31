/**
  ******************************************************************************
  * @file    w25qxx.c
 * @brief   W25Q 系列串行 NOR Flash Device 的识别与原始读写实现。
  *
 * @details
 *          当前定义启动识别和 Quad 能力配置的最小语义：经已绑定的总线读取并
 *          缓存 JEDEC 三字节 ID、校验实例注入的厂商与容量、探测 SFDP 头签名，
 *          并在 QE 未开启时安全写入 SR2。当前还提供 W25Q256 固定 4-byte
 *          Quad I/O 同步/非阻塞读取、非阻塞 Quad 页编程和 4 KiB Sector Erase
 *          启动和状态匹配轮询；FTL、内存映射和任意长度写入仍不属于本 Module。
  ******************************************************************************
  */

#include "Components/w25qxx/w25qxx.h"
#include "Components/w25qxx/w25qxx_config.h"

#include <stddef.h>

/** @brief SFDP 0x5A 的固定 24-bit 单线带地址读取阶段。 */
static const W25Qxx_BusAddressedTransferConfigTypeDef
    w25qxx_sfdp_transfer_config = {
        .AddressLength = W25QXX_SFDP_ADDRESS_LENGTH,
        .AddressLineMode = W25QXX_BUS_LINES_1,
        .DataLineMode = W25QXX_BUS_LINES_1,
        .HasAlternateByte = false,
        .AlternateByte = 0u,
        .AlternateByteLineMode = W25QXX_BUS_LINES_1,
        .DummyCycles = W25QXX_SFDP_DUMMY_CYCLES
    };

/** @brief W25Q256 0xEC 的固定 4-byte 1-4-4 Quad I/O 读取阶段。 */
static const W25Qxx_BusAddressedTransferConfigTypeDef
    w25qxx_quad_io_read_transfer_config = {
        .AddressLength = W25QXX_ARRAY_ADDRESS_LENGTH,
        .AddressLineMode = W25QXX_BUS_LINES_4,
        .DataLineMode = W25QXX_BUS_LINES_4,
        .HasAlternateByte = true,
        .AlternateByte = W25QXX_FAST_READ_QUAD_IO_MODE_BYTE,
        .AlternateByteLineMode = W25QXX_BUS_LINES_4,
        .DummyCycles = W25QXX_FAST_READ_QUAD_IO_DUMMY_CYCLES
    };

/** @brief W25Q256 0x34 的固定 4-byte 1-1-4 Quad 页编程阶段。 */
static const W25Qxx_BusAddressedTransferConfigTypeDef
    w25qxx_quad_page_program_transfer_config = {
        .AddressLength = W25QXX_ARRAY_ADDRESS_LENGTH,
        .AddressLineMode = W25QXX_BUS_LINES_1,
        .DataLineMode = W25QXX_BUS_LINES_4,
        .HasAlternateByte = false,
        .AlternateByte = 0u,
        .AlternateByteLineMode = W25QXX_BUS_LINES_1,
        .DummyCycles = 0u
    };

/** @brief W25Q256 0x21 的固定 4-byte 单线 4 KiB Sector Erase 阶段。 */
static const W25Qxx_BusAddressedTransferConfigTypeDef
    w25qxx_sector_erase_transfer_config = {
        .AddressLength = W25QXX_ARRAY_ADDRESS_LENGTH,
        .AddressLineMode = W25QXX_BUS_LINES_1,
        .DataLineMode = W25QXX_BUS_LINES_1,
        .HasAlternateByte = false,
        .AlternateByte = 0u,
        .AlternateByteLineMode = W25QXX_BUS_LINES_1,
        .DummyCycles = 0u
    };

/**
 * @brief 记录一次语义失败并把 Device 置为错误状态。
 * @param hflash 已验证非空的 W25Qxx Handle。
 * @param error 本次失败对应的 Device 语义阶段。
 * @param bus_status Adapter 返回的归一化底层总线状态。
 * @retval W25QXX_ERROR。
 */
static W25Qxx_StatusTypeDef w25qxx_record_failure(
    W25Qxx_HandleTypeDef *hflash,
    W25Qxx_ErrorTypeDef error,
    W25Qxx_BusStatusTypeDef bus_status)
{
    hflash->ErrorCode = error;
    hflash->LastBusStatus = bus_status;
    hflash->ActiveOperation = W25QXX_OPERATION_NONE;
    hflash->ActiveOperationStartTickMs = 0u;
    hflash->State = W25QXX_STATE_ERROR;
    return W25QXX_ERROR;
}

/**
 * @brief 在不改变当前 Device 生命周期状态的条件下读取两个状态寄存器。
 * @param hflash 已完成总线绑定的 W25Qxx Handle。
 * @param status_registers 接收状态快照的有效地址。
 * @retval W25QXX_OK 已成功解析 SR1、SR2。
 * @retval W25QXX_ERROR 总线接缝未绑定，或任一状态读取失败。
 * @note 本私有函数同时供 READY 状态下的公开查询和 BUSY 状态下的异步页编程
 *       轮询使用；因此不能在内部把 State 直接切回 READY。
 */
static W25Qxx_StatusTypeDef w25qxx_read_status_registers_internal(
    W25Qxx_HandleTypeDef *hflash,
    W25Qxx_StatusRegistersTypeDef *status_registers)
{
    W25Qxx_BusStatusTypeDef bus_status;
    uint8_t status_register_1;
    uint8_t status_register_2;

    if ((hflash->BusOps == NULL) ||
        (hflash->BusContext == NULL) ||
        (hflash->BusOps->ReadCommand == NULL))
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_BUS_NOT_BOUND,
                                     W25QXX_BUS_ERROR);
    }

    bus_status = hflash->BusOps->ReadCommand(
        hflash->BusContext,
        W25QXX_COMMAND_READ_STATUS_REGISTER_1,
        &status_register_1,
        sizeof(status_register_1));
    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_READ_STATUS_REGISTER_1,
                                     bus_status);
    }

    bus_status = hflash->BusOps->ReadCommand(
        hflash->BusContext,
        W25QXX_COMMAND_READ_STATUS_REGISTER_2,
        &status_register_2,
        sizeof(status_register_2));
    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_READ_STATUS_REGISTER_2,
                                     bus_status);
    }

    status_registers->StatusRegister1 = status_register_1;
    status_registers->StatusRegister2 = status_register_2;
    status_registers->IsWriteInProgress =
        ((status_register_1 & W25QXX_STATUS_REGISTER_1_WIP_MASK) != 0u);
    status_registers->IsWriteEnabled =
        ((status_register_1 & W25QXX_STATUS_REGISTER_1_WEL_MASK) != 0u);
    status_registers->IsQuadEnabled =
        ((status_register_2 & W25QXX_STATUS_REGISTER_2_QE_MASK) != 0u);
    hflash->ErrorCode = W25QXX_ERROR_NONE;
    hflash->LastBusStatus = W25QXX_BUS_OK;
    return W25QXX_OK;
}

/**
 * @brief 校验一段原始 Flash 数据区间是否落在当前已识别器件容量内。
 * @param hflash 已完成初始化的 W25Qxx Handle。
 * @param address 请求的首字节地址。
 * @param data_length 请求长度，必须非零。
 * @retval true 区间合法。
 * @retval false 长度为零、容量代码无效或区间越界；ErrorCode 已记录。
 */
static bool w25qxx_validate_array_request(
    W25Qxx_HandleTypeDef *hflash,
    uint32_t address,
    uint32_t data_length)
{
    uint32_t capacity_bytes;

    if (data_length == 0u)
    {
        hflash->ErrorCode = W25QXX_ERROR_INVALID_DATA_LENGTH;
        return false;
    }

    if ((hflash->JedecID.CapacityID < W25QXX_CAPACITY_ID_1MBIT) ||
        (hflash->JedecID.CapacityID > W25QXX_CAPACITY_ID_256MBIT))
    {
        hflash->ErrorCode = W25QXX_ERROR_INVALID_ADDRESS;
        return false;
    }

    capacity_bytes = UINT32_C(1) << hflash->JedecID.CapacityID;
    if ((address >= capacity_bytes) ||
        (data_length > (capacity_bytes - address)))
    {
        hflash->ErrorCode = W25QXX_ERROR_INVALID_ADDRESS;
        return false;
    }

    return true;
}

/**
 * @brief 判断当前已识别器件是否支持本垂直切片固定使用的四字节 Quad 指令。
 * @param hflash 已完成初始化的 W25Qxx Handle。
 * @retval true 当前为 W25Q256（容量码 0x19）。
 * @retval false 当前容量尚未具备对应的命令描述，ErrorCode 已记录。
 * @note 容量代码宏可用于全系列 JEDEC 识别；但 0xEC/0x34 是本阶段仅针对
 *       W25Q256JV 固定四字节地址方案。较小容量器件需要单独的三字节命令描述，
 *       不能因“已识别”而误发当前命令。
 */
static bool w25qxx_supports_fixed_4byte_quad_operations(
    W25Qxx_HandleTypeDef *hflash)
{
    if (hflash->JedecID.CapacityID != W25QXX_CAPACITY_ID_256MBIT)
    {
        hflash->ErrorCode = W25QXX_ERROR_UNSUPPORTED_ARRAY_OPERATION;
        return false;
    }

    return true;
}

/**
 * @brief 轮询状态寄存器，等待刚提交的非易失 SR2 写入完成。
 * @param hflash 已验证可用的 W25Qxx Handle。
 * @param status_registers 接收最后一次 WIP=0 状态快照的有效地址。
 * @retval W25QXX_OK 已观察到 WIP 清零，且返回最终状态快照。
 * @retval W25QXX_ERROR 状态读取失败，或写状态寄存器超过有界等待时间。
 * @note  仅供启动期 QE 配置使用。W25Q256JV 的状态寄存器写入 tW 最大为 15 ms，
 *        此处使用 20 ms 上限；它不是未来页编程、擦除或 MSC 路径的轮询机制。
 */
static W25Qxx_StatusTypeDef w25qxx_wait_for_status_register_write(
    W25Qxx_HandleTypeDef *hflash,
    W25Qxx_StatusRegistersTypeDef *status_registers)
{
    uint32_t start_tick_ms;

    start_tick_ms = hflash->BusOps->GetTickMs(hflash->BusContext);

    for (;;)
    {
        if (W25Qxx_ReadStatusRegisters(hflash, status_registers) != W25QXX_OK)
        {
            return W25QXX_ERROR;
        }

        if (!status_registers->IsWriteInProgress)
        {
            return W25QXX_OK;
        }

        if ((uint32_t)(hflash->BusOps->GetTickMs(hflash->BusContext) -
                       start_tick_ms) >=
            W25QXX_STATUS_REGISTER_WRITE_TIMEOUT_MS)
        {
            return w25qxx_record_failure(
                hflash,
                W25QXX_ERROR_STATUS_REGISTER_WRITE_TIMEOUT,
                W25QXX_BUS_TIMEOUT);
        }
    }
}

/**
 * @brief  初始化 W25Qxx Device 并读取 JEDEC 三字节芯片标识。
 * @param  hflash 已由 Platform 绑定 BusOps 和 BusContext 的 W25Qxx Handle。
 * @retval W25QXX_OK 已成功读取且缓存 JEDEC ID，Device 进入 READY。
 * @retval W25QXX_ERROR Handle、配置或绑定无效，JEDEC 读取失败，或芯片标识
 *         与当前实例的预期厂商和容量不符。
 * @note   本函数同步阻塞于一次极短的无地址 QSPI 命令，仅应在调度器启动前的
 *         Platform 初始化路径调用。ExpectedJedecID 由 Platform 注入当前 PCB
 *         的厂商与容量要求；MemoryType 仅缓存，不作为匹配条件。
 */
W25Qxx_StatusTypeDef W25Qxx_Init(W25Qxx_HandleTypeDef *hflash)
{
    W25Qxx_BusStatusTypeDef bus_status;
    uint8_t raw_jedec_id[W25QXX_JEDEC_ID_LENGTH];

    if (hflash == NULL)
    {
        return W25QXX_ERROR;
    }

    hflash->State = W25QXX_STATE_RESET;
    hflash->ErrorCode = W25QXX_ERROR_NONE;
    hflash->LastBusStatus = W25QXX_BUS_OK;
    hflash->ActiveOperation = W25QXX_OPERATION_NONE;
    hflash->ActiveOperationStartTickMs = 0u;

    if ((hflash->BusOps == NULL) ||
        (hflash->BusContext == NULL) ||
        (hflash->BusOps->ReadCommand == NULL))
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_BUS_NOT_BOUND,
                                     W25QXX_BUS_ERROR);
    }

    if (hflash->ExpectedJedecID == NULL)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_INVALID_PARAM,
                                     W25QXX_BUS_OK);
    }

    hflash->State = W25QXX_STATE_BUSY;
    bus_status = hflash->BusOps->ReadCommand(
        hflash->BusContext,
        W25QXX_COMMAND_READ_JEDEC_ID,
        raw_jedec_id,
        W25QXX_JEDEC_ID_LENGTH);
    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_READ_JEDEC_ID,
                                     bus_status);
    }

    hflash->JedecID.ManufacturerID = raw_jedec_id[0];
    hflash->JedecID.MemoryType = raw_jedec_id[1];
    hflash->JedecID.CapacityID = raw_jedec_id[2];

    if ((hflash->JedecID.ManufacturerID !=
         hflash->ExpectedJedecID->ManufacturerID) ||
        (hflash->JedecID.CapacityID !=
         hflash->ExpectedJedecID->CapacityID))
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_CHIP_MISMATCH,
                                     W25QXX_BUS_OK);
    }

    hflash->ErrorCode = W25QXX_ERROR_NONE;
    hflash->LastBusStatus = W25QXX_BUS_OK;
    hflash->State = W25QXX_STATE_READY;
    return W25QXX_OK;
}

/**
 * @brief  获取初始化阶段成功缓存的 JEDEC 三字节芯片标识。
 * @param  hflash 已完成初始化的 W25Qxx Device Handle。
 * @param  jedec_id 接收制造商、存储器类型和容量代码的有效地址。
 * @retval W25QXX_OK 已复制缓存的 JEDEC ID。
 * @retval W25QXX_ERROR 参数无效，或 Device 尚未处于 READY。
 * @note   本函数不重新访问总线；若以后需要诊断性重复读取，应以单独的显式 API
 *         表达并定义其并发与失败语义。
 */
W25Qxx_StatusTypeDef W25Qxx_GetJedecID(
    W25Qxx_HandleTypeDef *hflash,
    W25Qxx_JedecIDTypeDef *jedec_id)
{
    if ((hflash == NULL) || (jedec_id == NULL))
    {
        if (hflash != NULL)
        {
            hflash->ErrorCode = W25QXX_ERROR_INVALID_PARAM;
        }

        return W25QXX_ERROR;
    }

    if (hflash->State != W25QXX_STATE_READY)
    {
        hflash->ErrorCode = W25QXX_ERROR_NOT_READY;
        return W25QXX_ERROR;
    }

    *jedec_id = hflash->JedecID;
    hflash->ErrorCode = W25QXX_ERROR_NONE;
    return W25QXX_OK;
}

/**
 * @brief  读取并校验 Serial Flash Discoverable Parameters（SFDP）头签名。
 * @param  hflash 已完成 JEDEC 初始化的 W25Qxx Device Handle。
 * @retval W25QXX_OK 已从地址 0 读取到有效的 "SFDP" 签名，Device 保持 READY。
 * @retval W25QXX_ERROR Device 未就绪、带地址读接缝未绑定、总线读取失败，或签名
 *         不正确。
 * @note   本函数使用 0x5A、24-bit 地址 0 和 8 个 dummy cycle。该路径不读取
 *         用户数据，也不会改变 Flash 内容；它只验证带地址的间接读事务。
 */
W25Qxx_StatusTypeDef W25Qxx_ProbeSFDP(W25Qxx_HandleTypeDef *hflash)
{
    W25Qxx_BusStatusTypeDef bus_status;
    uint8_t sfdp_signature[W25QXX_SFDP_SIGNATURE_LENGTH];

    if (hflash == NULL)
    {
        return W25QXX_ERROR;
    }

    if (hflash->State != W25QXX_STATE_READY)
    {
        hflash->ErrorCode = W25QXX_ERROR_NOT_READY;
        return W25QXX_ERROR;
    }

    if ((hflash->BusOps == NULL) ||
        (hflash->BusContext == NULL) ||
        (hflash->BusOps->ReadAddressedCommand == NULL))
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_BUS_NOT_BOUND,
                                     W25QXX_BUS_ERROR);
    }

    hflash->State = W25QXX_STATE_BUSY;
    bus_status = hflash->BusOps->ReadAddressedCommand(
        hflash->BusContext,
        W25QXX_COMMAND_READ_SFDP,
        W25QXX_SFDP_ADDRESS,
        &w25qxx_sfdp_transfer_config,
        sfdp_signature,
        W25QXX_SFDP_SIGNATURE_LENGTH);
    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_READ_SFDP,
                                     bus_status);
    }

    if ((sfdp_signature[0] != W25QXX_SFDP_SIGNATURE_BYTE_0) ||
        (sfdp_signature[1] != W25QXX_SFDP_SIGNATURE_BYTE_1) ||
        (sfdp_signature[2] != W25QXX_SFDP_SIGNATURE_BYTE_2) ||
        (sfdp_signature[3] != W25QXX_SFDP_SIGNATURE_BYTE_3))
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_INVALID_SFDP_SIGNATURE,
                                     W25QXX_BUS_OK);
    }

    hflash->ErrorCode = W25QXX_ERROR_NONE;
    hflash->LastBusStatus = W25QXX_BUS_OK;
    hflash->State = W25QXX_STATE_READY;
    return W25QXX_OK;
}

/**
 * @brief  读取并解析 W25Qxx 的状态寄存器 1 和状态寄存器 2。
 * @param  hflash 已完成 JEDEC 初始化的 W25Qxx Device Handle。
 * @param  status_registers 接收原始寄存器值及 WIP、WEL、QE 语义的有效地址。
 * @retval W25QXX_OK 已完成两次无地址单线读取并解析状态，Device 保持 READY。
 * @retval W25QXX_ERROR 参数无效、Device 未就绪、无地址读接缝未绑定，或任一
 *         状态寄存器读取失败。
 * @note   本函数不会改变 Flash 状态；WIP/WEL 是瞬态状态，不在 Handle 中缓存。
 *         后续写入状态机应在需要判断时重新调用本函数。
 */
W25Qxx_StatusTypeDef W25Qxx_ReadStatusRegisters(
    W25Qxx_HandleTypeDef *hflash,
    W25Qxx_StatusRegistersTypeDef *status_registers)
{
    if ((hflash == NULL) || (status_registers == NULL))
    {
        if (hflash != NULL)
        {
            hflash->ErrorCode = W25QXX_ERROR_INVALID_PARAM;
        }

        return W25QXX_ERROR;
    }

    if (hflash->State != W25QXX_STATE_READY)
    {
        hflash->ErrorCode = W25QXX_ERROR_NOT_READY;
        return W25QXX_ERROR;
    }

    hflash->State = W25QXX_STATE_BUSY;
    if (w25qxx_read_status_registers_internal(hflash, status_registers) !=
        W25QXX_OK)
    {
        return W25QXX_ERROR;
    }

    hflash->State = W25QXX_STATE_READY;
    return W25QXX_OK;
}

/**
 * @brief  确保 W25Qxx 的 Quad Enable（QE）位已开启。
 * @param  hflash 已完成 JEDEC 初始化的 W25Qxx Device Handle。
 * @retval W25QXX_OK SR2.QE 已确认或已安全置位，Device 保持 READY。
 * @retval W25QXX_ERROR 参数无效、状态读取或写入失败、Flash 忙、WEL 未锁存、
 *         状态寄存器写入超时，或写后 QE 仍未开启。
 * @note   初始 QE 已为 1 时不写 Flash。QE 为 0 时，本函数仅在 WIP=0 条件下依次
 *         发送 0x06、确认 WEL、以 0x31 回写原 SR2|QE，并在 20 ms 内轮询 WIP。
 *         它只适合调度器前的启动期能力配置；未来页编程、擦除与 MSC 路径必须使用
 *         独立的异步状态机，不能复用本函数的阻塞轮询。
 */
W25Qxx_StatusTypeDef W25Qxx_EnsureQuadEnabled(
    W25Qxx_HandleTypeDef *hflash)
{
    W25Qxx_StatusRegistersTypeDef status_registers;
    W25Qxx_BusStatusTypeDef bus_status;
    uint8_t status_register_2_to_write;

    if (hflash == NULL)
    {
        return W25QXX_ERROR;
    }

    if (W25Qxx_ReadStatusRegisters(hflash, &status_registers) != W25QXX_OK)
    {
        return W25QXX_ERROR;
    }

    if (status_registers.IsQuadEnabled)
    {
        return W25QXX_OK;
    }

    if (status_registers.IsWriteInProgress)
    {
        hflash->ErrorCode = W25QXX_ERROR_FLASH_BUSY;
        return W25QXX_ERROR;
    }

    if ((hflash->BusOps == NULL) ||
        (hflash->BusContext == NULL) ||
        (hflash->BusOps->ExecuteCommand == NULL) ||
        (hflash->BusOps->WriteCommand == NULL) ||
        (hflash->BusOps->GetTickMs == NULL))
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_BUS_NOT_BOUND,
                                     W25QXX_BUS_ERROR);
    }

    hflash->State = W25QXX_STATE_BUSY;
    bus_status = hflash->BusOps->ExecuteCommand(
        hflash->BusContext,
        W25QXX_COMMAND_WRITE_ENABLE);
    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_WRITE_ENABLE,
                                     bus_status);
    }

    hflash->LastBusStatus = W25QXX_BUS_OK;
    hflash->State = W25QXX_STATE_READY;

    if (W25Qxx_ReadStatusRegisters(hflash, &status_registers) != W25QXX_OK)
    {
        return W25QXX_ERROR;
    }

    if (!status_registers.IsWriteEnabled)
    {
        hflash->ErrorCode = W25QXX_ERROR_WRITE_ENABLE_NOT_LATCHED;
        return W25QXX_ERROR;
    }

    status_register_2_to_write =
        (uint8_t)(status_registers.StatusRegister2 |
                  W25QXX_STATUS_REGISTER_2_QE_MASK);
    hflash->State = W25QXX_STATE_BUSY;
    bus_status = hflash->BusOps->WriteCommand(
        hflash->BusContext,
        W25QXX_COMMAND_WRITE_STATUS_REGISTER_2,
        &status_register_2_to_write,
        sizeof(status_register_2_to_write));
    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_WRITE_STATUS_REGISTER_2,
                                     bus_status);
    }

    hflash->LastBusStatus = W25QXX_BUS_OK;
    hflash->State = W25QXX_STATE_READY;

    if (w25qxx_wait_for_status_register_write(hflash, &status_registers) !=
        W25QXX_OK)
    {
        return W25QXX_ERROR;
    }

    if (!status_registers.IsQuadEnabled)
    {
        hflash->ErrorCode = W25QXX_ERROR_QUAD_NOT_ENABLED;
        return W25QXX_ERROR;
    }

    hflash->ErrorCode = W25QXX_ERROR_NONE;
    return W25QXX_OK;
}

/**
 * @brief 获取当前已识别 W25Q256 的固定 Quad I/O 数组读取协议描述。
 * @param hflash 已完成初始化且处于 READY 的 W25Qxx Device Handle。
 * @param protocol 接收指令与各事务阶段描述的有效地址。
 * @retval W25QXX_OK 已写入 `0xEC` 的 4-byte `1-4-4` 读取协议。
 * @retval W25QXX_ERROR 参数无效、Device 未就绪或当前容量不支持固定四字节协议。
 * @note 此 Interface 仅公开 NOR 读取协议，不改变 Device 状态，也不进入任何
 *       控制器的内存映射模式。Platform 可将该描述交给具体 QSPI Adapter。
 */
W25Qxx_StatusTypeDef W25Qxx_GetArrayReadProtocol(
    W25Qxx_HandleTypeDef *hflash,
    W25Qxx_ArrayReadProtocolTypeDef *protocol)
{
    if ((hflash == NULL) || (protocol == NULL))
    {
        if (hflash != NULL)
        {
            hflash->ErrorCode = W25QXX_ERROR_INVALID_PARAM;
        }

        return W25QXX_ERROR;
    }

    if (hflash->State != W25QXX_STATE_READY)
    {
        hflash->ErrorCode = W25QXX_ERROR_NOT_READY;
        return W25QXX_ERROR;
    }

    if (!w25qxx_supports_fixed_4byte_quad_operations(hflash))
    {
        return W25QXX_ERROR;
    }

    protocol->Instruction = W25QXX_COMMAND_FAST_READ_QUAD_IO_4BYTE;
    protocol->TransferConfig = w25qxx_quad_io_read_transfer_config;
    hflash->ErrorCode = W25QXX_ERROR_NONE;
    hflash->LastBusStatus = W25QXX_BUS_OK;
    return W25QXX_OK;
}

/**
 * @brief 以 W25Q256 0xEC 固定 4-byte Quad I/O 命令读取原始 Flash 数据。
 * @param hflash 已完成初始化和 QE 配置的 W25Qxx Device Handle。
 * @param address 待读取首字节的物理 Flash 地址，必须 4-byte 对齐。
 * @param data 接收数据的有效缓冲区。
 * @param data_length 待读取字节数，必须非零且完整落在当前识别容量内。
 * @retval W25QXX_OK 数据已同步传入 data，Device 回到 READY。
 * @retval W25QXX_ERROR 参数、对齐或地址范围无效，Device 未就绪，BusOps 未绑定，
 *         或 QSPI 间接读取失败。
 * @note W25Q256 的 0xEC 为 1-4-4 事务：发送 32-bit 四线地址，随后四线发送
 *       连续读取模式字节 0xFF 和 4 个 dummy clock，再四线接收数据。0xEC 要求
 *       起始 A1/A0 为 0/0，因此此原始 API 不接受非 4-byte 对齐首地址。FTL
 *       后续以扇区对齐读写，能天然满足该约束；若以后需要任意字节读，应由更高层
 *       显式实现对齐扩展与裁剪，不能让 Adapter 静默篡改命令语义。
 */
W25Qxx_StatusTypeDef W25Qxx_Read(
    W25Qxx_HandleTypeDef *hflash,
    uint32_t address,
    uint8_t *data,
    uint32_t data_length)
{
    W25Qxx_BusStatusTypeDef bus_status;

    if ((hflash == NULL) || (data == NULL))
    {
        if (hflash != NULL)
        {
            hflash->ErrorCode = W25QXX_ERROR_INVALID_PARAM;
        }

        return W25QXX_ERROR;
    }

    if (hflash->State != W25QXX_STATE_READY)
    {
        hflash->ErrorCode = W25QXX_ERROR_NOT_READY;
        return W25QXX_ERROR;
    }

    if (!w25qxx_validate_array_request(hflash, address, data_length))
    {
        return W25QXX_ERROR;
    }

    if (!w25qxx_supports_fixed_4byte_quad_operations(hflash))
    {
        return W25QXX_ERROR;
    }

    if ((address % W25QXX_QUAD_READ_ADDRESS_ALIGNMENT_BYTES) != 0u)
    {
        hflash->ErrorCode = W25QXX_ERROR_INVALID_READ_ADDRESS_ALIGNMENT;
        return W25QXX_ERROR;
    }

    if ((hflash->BusOps == NULL) ||
        (hflash->BusContext == NULL) ||
        (hflash->BusOps->ReadAddressedCommand == NULL))
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_BUS_NOT_BOUND,
                                     W25QXX_BUS_ERROR);
    }

    hflash->State = W25QXX_STATE_BUSY;
    bus_status = hflash->BusOps->ReadAddressedCommand(
        hflash->BusContext,
        W25QXX_COMMAND_FAST_READ_QUAD_IO_4BYTE,
        address,
        &w25qxx_quad_io_read_transfer_config,
        data,
        data_length);
    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_READ_ARRAY,
                                     bus_status);
    }

    hflash->ErrorCode = W25QXX_ERROR_NONE;
    hflash->LastBusStatus = W25QXX_BUS_OK;
    hflash->State = W25QXX_STATE_READY;
    return W25QXX_OK;
}

/**
 * @brief 启动一次 W25Q256 0xEC 固定 4-byte Quad I/O 非阻塞原始读取。
 * @param hflash 已完成初始化和 QE 配置的 W25Qxx Device Handle。
 * @param address 待读取首字节的物理 Flash 地址，必须 4-byte 对齐。
 * @param data 接收 DMA 或等价异步总线搬运数据的有效缓冲区。
 * @param data_length 待读取字节数，必须非零且完整落在当前识别容量内。
 * @retval W25QXX_OK 底层已接受读取请求，Device 转入 BUSY；调用者应在完成通知
 *         后于普通上下文调用 W25Qxx_Process() 收尾。
 * @retval W25QXX_ERROR 参数、对齐或地址范围无效，Device 未就绪，异步总线接缝
 *         未绑定，或底层拒绝启动。
 * @note 与 W25Qxx_Read() 使用相同的 0xEC 协议和边界校验；区别仅在数据搬运的
 *       生命周期。该函数不访问状态寄存器，因为数组读取不会改变 Flash 内部 WIP。
 *       Adapter 必须在接收完成后才让状态查询返回 OK，并在普通上下文完成 Cache
 *       失效，以保证调用者可安全读取 data。
 */
W25Qxx_StatusTypeDef W25Qxx_StartRead(
    W25Qxx_HandleTypeDef *hflash,
    uint32_t address,
    uint8_t *data,
    uint32_t data_length)
{
    W25Qxx_BusStatusTypeDef bus_status;

    if ((hflash == NULL) || (data == NULL))
    {
        if (hflash != NULL)
        {
            hflash->ErrorCode = W25QXX_ERROR_INVALID_PARAM;
        }

        return W25QXX_ERROR;
    }

    if (hflash->State != W25QXX_STATE_READY)
    {
        hflash->ErrorCode = W25QXX_ERROR_NOT_READY;
        return W25QXX_ERROR;
    }

    if (!w25qxx_validate_array_request(hflash, address, data_length))
    {
        return W25QXX_ERROR;
    }

    if (!w25qxx_supports_fixed_4byte_quad_operations(hflash))
    {
        return W25QXX_ERROR;
    }

    if ((address % W25QXX_QUAD_READ_ADDRESS_ALIGNMENT_BYTES) != 0u)
    {
        hflash->ErrorCode = W25QXX_ERROR_INVALID_READ_ADDRESS_ALIGNMENT;
        return W25QXX_ERROR;
    }

    if ((hflash->BusOps == NULL) ||
        (hflash->BusContext == NULL) ||
        (hflash->BusOps->StartReadAddressedCommand == NULL) ||
        (hflash->BusOps->GetReadAddressedCommandStatus == NULL) ||
        (hflash->BusOps->GetTickMs == NULL))
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_BUS_NOT_BOUND,
                                     W25QXX_BUS_ERROR);
    }

    hflash->State = W25QXX_STATE_BUSY;
    bus_status = hflash->BusOps->StartReadAddressedCommand(
        hflash->BusContext,
        W25QXX_COMMAND_FAST_READ_QUAD_IO_4BYTE,
        address,
        &w25qxx_quad_io_read_transfer_config,
        data,
        data_length);
    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_READ_ARRAY,
                                     bus_status);
    }

    hflash->ActiveOperation = W25QXX_OPERATION_ARRAY_READ;
    hflash->ActiveOperationStartTickMs = hflash->BusOps->GetTickMs(
        hflash->BusContext);
    hflash->ErrorCode = W25QXX_ERROR_NONE;
    hflash->LastBusStatus = W25QXX_BUS_OK;
    return W25QXX_OK;
}

/**
 * @brief 启动一次 W25Q256 0x34 固定 4-byte Quad 页编程事务。
 * @param hflash 已完成初始化和 QE 配置的 W25Qxx Device Handle。
 * @param address 待写入首字节的物理 Flash 地址。
 * @param data 待写入数据的有效缓冲区。
 * @param data_length 待写入字节数，范围为 1..256，且不得跨越物理页边界。
 * @retval W25QXX_OK 命令和数据已传入 Flash，且 WIP 自动轮询已经启动；Device
 *         转入 BUSY，调用者应在 Status Match 通知后调用 W25Qxx_Process() 收尾。
 * @retval W25QXX_ERROR 参数、页边界或地址范围无效，Device 未就绪、Flash 已忙、
 *         QE/WEL 状态不正确，或底层命令传输失败。
 * @note 该函数不等待 tPP，因而不能直接确认数据已非易失化。调用前目标字节必须
 *       已被擦除；擦除、任意长度拆页和介质一致性属于后续 FTL 的职责。选择 0x34
 *       可固定使用 32-bit 地址，不依赖全局 4-byte Address Mode。
 */
W25Qxx_StatusTypeDef W25Qxx_ProgramPageStart(
    W25Qxx_HandleTypeDef *hflash,
    uint32_t address,
    const uint8_t *data,
    uint32_t data_length)
{
    W25Qxx_BusStatusTypeDef bus_status;
    W25Qxx_StatusRegistersTypeDef status_registers;
    uint32_t page_offset;

    if ((hflash == NULL) || (data == NULL))
    {
        if (hflash != NULL)
        {
            hflash->ErrorCode = W25QXX_ERROR_INVALID_PARAM;
        }

        return W25QXX_ERROR;
    }

    if (hflash->State != W25QXX_STATE_READY)
    {
        hflash->ErrorCode = W25QXX_ERROR_NOT_READY;
        return W25QXX_ERROR;
    }

    if (!w25qxx_validate_array_request(hflash, address, data_length))
    {
        return W25QXX_ERROR;
    }

    if (!w25qxx_supports_fixed_4byte_quad_operations(hflash))
    {
        return W25QXX_ERROR;
    }

    if (data_length > W25QXX_PAGE_PROGRAM_MAX_SIZE_BYTES)
    {
        hflash->ErrorCode = W25QXX_ERROR_INVALID_DATA_LENGTH;
        return W25QXX_ERROR;
    }

    page_offset = address % W25QXX_PAGE_PROGRAM_MAX_SIZE_BYTES;
    if (data_length > (W25QXX_PAGE_PROGRAM_MAX_SIZE_BYTES - page_offset))
    {
        hflash->ErrorCode = W25QXX_ERROR_PAGE_BOUNDARY;
        return W25QXX_ERROR;
    }

    if ((hflash->BusOps == NULL) ||
        (hflash->BusContext == NULL) ||
        (hflash->BusOps->ReadCommand == NULL) ||
        (hflash->BusOps->ExecuteCommand == NULL) ||
        (hflash->BusOps->WriteAddressedCommand == NULL) ||
        (hflash->BusOps->StartStatusMatchPolling == NULL) ||
        (hflash->BusOps->GetStatusMatchPollingStatus == NULL) ||
        (hflash->BusOps->AbortStatusMatchPolling == NULL) ||
        (hflash->BusOps->GetTickMs == NULL))
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_BUS_NOT_BOUND,
                                     W25QXX_BUS_ERROR);
    }

    if (W25Qxx_ReadStatusRegisters(hflash, &status_registers) != W25QXX_OK)
    {
        return W25QXX_ERROR;
    }

    if (status_registers.IsWriteInProgress)
    {
        hflash->ErrorCode = W25QXX_ERROR_FLASH_BUSY;
        return W25QXX_ERROR;
    }

    if (!status_registers.IsQuadEnabled)
    {
        hflash->ErrorCode = W25QXX_ERROR_QUAD_NOT_ENABLED;
        return W25QXX_ERROR;
    }

    hflash->State = W25QXX_STATE_BUSY;
    bus_status = hflash->BusOps->ExecuteCommand(
        hflash->BusContext,
        W25QXX_COMMAND_WRITE_ENABLE);
    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_WRITE_ENABLE,
                                     bus_status);
    }

    hflash->LastBusStatus = W25QXX_BUS_OK;
    hflash->State = W25QXX_STATE_READY;
    if (W25Qxx_ReadStatusRegisters(hflash, &status_registers) != W25QXX_OK)
    {
        return W25QXX_ERROR;
    }

    if (status_registers.IsWriteInProgress)
    {
        hflash->ErrorCode = W25QXX_ERROR_FLASH_BUSY;
        return W25QXX_ERROR;
    }

    if (!status_registers.IsWriteEnabled)
    {
        hflash->ErrorCode = W25QXX_ERROR_WRITE_ENABLE_NOT_LATCHED;
        return W25QXX_ERROR;
    }

    hflash->State = W25QXX_STATE_BUSY;
    bus_status = hflash->BusOps->WriteAddressedCommand(
        hflash->BusContext,
        W25QXX_COMMAND_QUAD_PAGE_PROGRAM_4BYTE,
        address,
        &w25qxx_quad_page_program_transfer_config,
        data,
        data_length);
    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_PROGRAM_PAGE,
                                     bus_status);
    }

    hflash->ActiveOperation = W25QXX_OPERATION_PAGE_PROGRAM;
    hflash->ActiveOperationStartTickMs = hflash->BusOps->GetTickMs(
        hflash->BusContext);
    bus_status = hflash->BusOps->StartStatusMatchPolling(
        hflash->BusContext,
        W25QXX_COMMAND_READ_STATUS_REGISTER_1,
        0u,
        W25QXX_STATUS_REGISTER_1_WIP_MASK);
    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_PROGRAM_PAGE,
                                     bus_status);
    }

    hflash->ErrorCode = W25QXX_ERROR_NONE;
    hflash->LastBusStatus = W25QXX_BUS_OK;
    return W25QXX_OK;
}

/**
 * @brief 启动一次 W25Q256 0x21 固定 4-byte 4 KiB Sector Erase 事务。
 * @param hflash 已完成初始化的 W25Qxx Device Handle。
 * @param address 待擦除 4 KiB 扇区的首地址，必须按 4 KiB 对齐。
 * @retval W25QXX_OK 擦除命令已送入 Flash，且 WIP 自动轮询已经启动；Device
 *         转入 BUSY，调用者应在 Status Match 通知后调用 W25Qxx_Process() 收尾。
 * @retval W25QXX_ERROR 参数、扇区边界或地址范围无效，Device 未就绪、Flash 已忙、
 *         WEL 未锁存，或底层命令传输失败。
 * @note 该函数不等待 tSE。0x21 使用独立 32-bit 地址，不依赖全局 4-byte Address
 *       Mode；当前仅由 Platform 的显式自检区诊断使用，FTL 将来复用同一异步语义。
 */
W25Qxx_StatusTypeDef W25Qxx_SectorEraseStart(
    W25Qxx_HandleTypeDef *hflash,
    uint32_t address)
{
    W25Qxx_BusStatusTypeDef bus_status;
    W25Qxx_StatusRegistersTypeDef status_registers;

    if (hflash == NULL)
    {
        return W25QXX_ERROR;
    }

    if (hflash->State != W25QXX_STATE_READY)
    {
        hflash->ErrorCode = W25QXX_ERROR_NOT_READY;
        return W25QXX_ERROR;
    }

    if (!w25qxx_validate_array_request(hflash,
                                        address,
                                        W25QXX_SECTOR_ERASE_SIZE_BYTES))
    {
        return W25QXX_ERROR;
    }

    if (!w25qxx_supports_fixed_4byte_quad_operations(hflash))
    {
        return W25QXX_ERROR;
    }

    if ((address % W25QXX_SECTOR_ERASE_SIZE_BYTES) != 0u)
    {
        hflash->ErrorCode = W25QXX_ERROR_SECTOR_BOUNDARY;
        return W25QXX_ERROR;
    }

    if ((hflash->BusOps == NULL) ||
        (hflash->BusContext == NULL) ||
        (hflash->BusOps->ReadCommand == NULL) ||
        (hflash->BusOps->ExecuteCommand == NULL) ||
        (hflash->BusOps->ExecuteAddressedCommand == NULL) ||
        (hflash->BusOps->StartStatusMatchPolling == NULL) ||
        (hflash->BusOps->GetStatusMatchPollingStatus == NULL) ||
        (hflash->BusOps->AbortStatusMatchPolling == NULL) ||
        (hflash->BusOps->GetTickMs == NULL))
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_BUS_NOT_BOUND,
                                     W25QXX_BUS_ERROR);
    }

    if (W25Qxx_ReadStatusRegisters(hflash, &status_registers) != W25QXX_OK)
    {
        return W25QXX_ERROR;
    }

    if (status_registers.IsWriteInProgress)
    {
        hflash->ErrorCode = W25QXX_ERROR_FLASH_BUSY;
        return W25QXX_ERROR;
    }

    hflash->State = W25QXX_STATE_BUSY;
    bus_status = hflash->BusOps->ExecuteCommand(
        hflash->BusContext,
        W25QXX_COMMAND_WRITE_ENABLE);
    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_WRITE_ENABLE,
                                     bus_status);
    }

    hflash->LastBusStatus = W25QXX_BUS_OK;
    hflash->State = W25QXX_STATE_READY;
    if (W25Qxx_ReadStatusRegisters(hflash, &status_registers) != W25QXX_OK)
    {
        return W25QXX_ERROR;
    }

    if (status_registers.IsWriteInProgress)
    {
        hflash->ErrorCode = W25QXX_ERROR_FLASH_BUSY;
        return W25QXX_ERROR;
    }

    if (!status_registers.IsWriteEnabled)
    {
        hflash->ErrorCode = W25QXX_ERROR_WRITE_ENABLE_NOT_LATCHED;
        return W25QXX_ERROR;
    }

    hflash->State = W25QXX_STATE_BUSY;
    bus_status = hflash->BusOps->ExecuteAddressedCommand(
        hflash->BusContext,
        W25QXX_COMMAND_SECTOR_ERASE_4BYTE,
        address,
        &w25qxx_sector_erase_transfer_config);
    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_ERASE_SECTOR,
                                     bus_status);
    }

    hflash->ActiveOperation = W25QXX_OPERATION_SECTOR_ERASE;
    hflash->ActiveOperationStartTickMs = hflash->BusOps->GetTickMs(
        hflash->BusContext);
    bus_status = hflash->BusOps->StartStatusMatchPolling(
        hflash->BusContext,
        W25QXX_COMMAND_READ_STATUS_REGISTER_1,
        0u,
        W25QXX_STATUS_REGISTER_1_WIP_MASK);
    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_ERASE_SECTOR,
                                     bus_status);
    }

    hflash->ErrorCode = W25QXX_ERROR_NONE;
    hflash->LastBusStatus = W25QXX_BUS_OK;
    return W25QXX_OK;
}

/**
 * @brief 推进当前异步原始 Flash 操作一次，不阻塞等待总线或 Flash 内部时序。
 * @param hflash 已由异步读取、页编程或 4 KiB 扇区擦除启动函数置为 BUSY 的
 *        Device Handle。
 * @retval W25QXX_BUSY 当前数组读取或状态匹配轮询尚未收到完成事件；调用者应在
 *         对应 IRQ 通知后再次调用。
 * @retval W25QXX_OK 当前操作已完成，Device 回到 READY。
 * @retval W25QXX_ERROR Handle/状态无效、总线状态查询失败，或当前操作超过有界时限。
 * @note 数组读取只查询 Adapter 由 IRQ 更新的传输状态，并由 Adapter 在此普通
 *       上下文执行 Cache 收尾；页编程和擦除只查询 Adapter 由 Status Match IRQ
 *       更新的结果，不会反复发出 0x05。软件时限到期时会中止 QSPI 轮询（不会
 *       中止 Flash 的内部写/擦），再把 Device 置 ERROR。本函数不调用延时函数、
 *       不循环等待。未来 Flash FTL 的后台任务必须等待完成事件；MSC 路径绝不能
 *       自行阻塞 tPP、tSE 或 DMA 完成。
 */
W25Qxx_StatusTypeDef W25Qxx_Process(W25Qxx_HandleTypeDef *hflash)
{
    uint32_t elapsed_ms;
    uint32_t timeout_ms;
    W25Qxx_ErrorTypeDef timeout_error;
    W25Qxx_ErrorTypeDef operation_error;
    W25Qxx_BusStatusTypeDef bus_status;

    if (hflash == NULL)
    {
        return W25QXX_ERROR;
    }

    if (hflash->State != W25QXX_STATE_BUSY)
    {
        hflash->ErrorCode = W25QXX_ERROR_NOT_READY;
        return W25QXX_ERROR;
    }

    switch (hflash->ActiveOperation)
    {
        case W25QXX_OPERATION_ARRAY_READ:
            timeout_ms = W25QXX_ARRAY_READ_TIMEOUT_MS;
            timeout_error = W25QXX_ERROR_READ_ARRAY_TIMEOUT;
            operation_error = W25QXX_ERROR_READ_ARRAY;
            break;

        case W25QXX_OPERATION_PAGE_PROGRAM:
            timeout_ms = W25QXX_PAGE_PROGRAM_TIMEOUT_MS;
            timeout_error = W25QXX_ERROR_PAGE_PROGRAM_TIMEOUT;
            operation_error = W25QXX_ERROR_PROGRAM_PAGE;
            break;

        case W25QXX_OPERATION_SECTOR_ERASE:
            timeout_ms = W25QXX_SECTOR_ERASE_TIMEOUT_MS;
            timeout_error = W25QXX_ERROR_SECTOR_ERASE_TIMEOUT;
            operation_error = W25QXX_ERROR_ERASE_SECTOR;
            break;

        default:
            hflash->ErrorCode = W25QXX_ERROR_NOT_READY;
            return W25QXX_ERROR;
    }

    if ((hflash->BusOps == NULL) ||
        (hflash->BusContext == NULL) ||
        (hflash->BusOps->GetTickMs == NULL))
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_BUS_NOT_BOUND,
                                     W25QXX_BUS_ERROR);
    }

    if (hflash->ActiveOperation == W25QXX_OPERATION_ARRAY_READ)
    {
        if (hflash->BusOps->GetReadAddressedCommandStatus == NULL)
        {
            return w25qxx_record_failure(hflash,
                                         W25QXX_ERROR_BUS_NOT_BOUND,
                                         W25QXX_BUS_ERROR);
        }

        bus_status = hflash->BusOps->GetReadAddressedCommandStatus(
            hflash->BusContext);
        if (bus_status == W25QXX_BUS_BUSY)
        {
            elapsed_ms = hflash->BusOps->GetTickMs(hflash->BusContext) -
                         hflash->ActiveOperationStartTickMs;
            if (elapsed_ms >= timeout_ms)
            {
                return w25qxx_record_failure(hflash,
                                             timeout_error,
                                             W25QXX_BUS_TIMEOUT);
            }

            return W25QXX_BUSY;
        }

        if (bus_status != W25QXX_BUS_OK)
        {
            return w25qxx_record_failure(hflash,
                                         W25QXX_ERROR_READ_ARRAY,
                                         bus_status);
        }

        hflash->ActiveOperation = W25QXX_OPERATION_NONE;
        hflash->ActiveOperationStartTickMs = 0u;
        hflash->ErrorCode = W25QXX_ERROR_NONE;
        hflash->LastBusStatus = W25QXX_BUS_OK;
        hflash->State = W25QXX_STATE_READY;
        return W25QXX_OK;
    }

    if ((hflash->BusOps->GetStatusMatchPollingStatus == NULL) ||
        (hflash->BusOps->AbortStatusMatchPolling == NULL))
    {
        return w25qxx_record_failure(hflash,
                                     W25QXX_ERROR_BUS_NOT_BOUND,
                                     W25QXX_BUS_ERROR);
    }

    bus_status = hflash->BusOps->GetStatusMatchPollingStatus(hflash->BusContext);
    if (bus_status == W25QXX_BUS_BUSY)
    {
        elapsed_ms = hflash->BusOps->GetTickMs(hflash->BusContext) -
                     hflash->ActiveOperationStartTickMs;
        if (elapsed_ms >= timeout_ms)
        {
            bus_status = hflash->BusOps->AbortStatusMatchPolling(
                hflash->BusContext);
            return w25qxx_record_failure(hflash,
                                         timeout_error,
                                         (bus_status == W25QXX_BUS_OK) ?
                                             W25QXX_BUS_TIMEOUT :
                                             bus_status);
        }

        return W25QXX_BUSY;
    }

    if (bus_status != W25QXX_BUS_OK)
    {
        return w25qxx_record_failure(hflash, operation_error, bus_status);
    }

    hflash->ActiveOperation = W25QXX_OPERATION_NONE;
    hflash->ActiveOperationStartTickMs = 0u;
    hflash->ErrorCode = W25QXX_ERROR_NONE;
    hflash->LastBusStatus = W25QXX_BUS_OK;
    hflash->State = W25QXX_STATE_READY;
    return W25QXX_OK;
}
