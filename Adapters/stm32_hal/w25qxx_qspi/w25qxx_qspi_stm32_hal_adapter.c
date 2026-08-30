/**
  ******************************************************************************
  * @file    w25qxx_qspi_stm32_hal_adapter.c
  * @brief   STM32 HAL QSPI 到 W25Qxx BusOps 的同步 Adapter 实现。
  *
  * @details
 *          本 Module 把 W25Qxx Device 发出的控制、读写命令映射为 HAL QSPI
 *          间接模式事务。当前服务 JEDEC ID、SFDP、状态寄存器、启动期 QE 配置、
 *          0xEC Quad I/O 读取和 0x34 Quad 页数据传输，不使用 DMA、自动状态
 *          轮询、内存映射或 QSPI IRQ 回调。
  ******************************************************************************
  */

#include "Adapters/stm32_hal/w25qxx_qspi/w25qxx_qspi_stm32_hal_adapter.h"

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief 将 STM32 HAL QSPI 返回状态转换为 W25Qxx 归一化总线状态。
 * @param hal_status HAL QSPI 调用的返回值。
 * @retval 对应的 W25Qxx 总线状态。
 */
static W25Qxx_BusStatusTypeDef w25qxx_qspi_stm32_hal_map_status(
    HAL_StatusTypeDef hal_status)
{
    switch (hal_status)
    {
        case HAL_OK:
            return W25QXX_BUS_OK;

        case HAL_BUSY:
            return W25QXX_BUS_BUSY;

        case HAL_TIMEOUT:
            return W25QXX_BUS_TIMEOUT;

        default:
            return W25QXX_BUS_ERROR;
    }
}

/**
 * @brief 把 Device 指定的带地址事务配置转换为 STM32 HAL QSPI 命令字段。
 * @param command 待填写的 HAL QSPI 命令结构。
 * @param instruction 当前 NOR Flash 命令。
 * @param address 当前命令的物理地址。
 * @param transfer_config 由 W25Qxx Device 指定的地址、交替字节和数据阶段配置。
 * @param data_length 数据阶段的字节数；零表示无数据阶段的带地址控制命令。
 * @retval true 转换成功。
 * @retval false 配置中存在当前 Adapter 不支持的地址长度或线数。
 * @note W25Qxx Device 决定协议；此处只进行 HAL 枚举映射。0xEC 的模式字节
 *       必须作为 AlternateByte 发送，不能误并入 DummyCycles。
 */
static bool w25qxx_qspi_stm32_hal_build_addressed_command(
    QSPI_CommandTypeDef *command,
    uint8_t instruction,
    uint32_t address,
    const W25Qxx_BusAddressedTransferConfigTypeDef *transfer_config,
    uint32_t data_length)
{
    if ((command == NULL) ||
        (transfer_config == NULL))
    {
        return false;
    }

    command->Instruction = instruction;
    command->InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command->Address = address;
    switch (transfer_config->AddressLength)
    {
        case 3u:
            command->AddressSize = QSPI_ADDRESS_24_BITS;
            break;

        case 4u:
            command->AddressSize = QSPI_ADDRESS_32_BITS;
            break;

        default:
            return false;
    }

    switch (transfer_config->AddressLineMode)
    {
        case W25QXX_BUS_LINES_1:
            command->AddressMode = QSPI_ADDRESS_1_LINE;
            break;

        case W25QXX_BUS_LINES_2:
            command->AddressMode = QSPI_ADDRESS_2_LINES;
            break;

        case W25QXX_BUS_LINES_4:
            command->AddressMode = QSPI_ADDRESS_4_LINES;
            break;

        default:
            return false;
    }

    if (data_length == 0u)
    {
        command->DataMode = QSPI_DATA_NONE;
    }
    else
    {
        switch (transfer_config->DataLineMode)
        {
            case W25QXX_BUS_LINES_1:
                command->DataMode = QSPI_DATA_1_LINE;
                break;

            case W25QXX_BUS_LINES_2:
                command->DataMode = QSPI_DATA_2_LINES;
                break;

            case W25QXX_BUS_LINES_4:
                command->DataMode = QSPI_DATA_4_LINES;
                break;

            default:
                return false;
        }
    }

    if (transfer_config->HasAlternateByte)
    {
        switch (transfer_config->AlternateByteLineMode)
        {
            case W25QXX_BUS_LINES_1:
                command->AlternateByteMode = QSPI_ALTERNATE_BYTES_1_LINE;
                break;

            case W25QXX_BUS_LINES_2:
                command->AlternateByteMode = QSPI_ALTERNATE_BYTES_2_LINES;
                break;

            case W25QXX_BUS_LINES_4:
                command->AlternateByteMode = QSPI_ALTERNATE_BYTES_4_LINES;
                break;

            default:
                return false;
        }

        command->AlternateBytes = transfer_config->AlternateByte;
        command->AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
    }
    else
    {
        command->AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    }

    command->DummyCycles = transfer_config->DummyCycles;
    command->NbData = data_length;
    command->DdrMode = QSPI_DDR_MODE_DISABLE;
    command->DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command->SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    return true;
}

/**
 * @brief 通过 HAL QSPI 间接模式执行一条无地址的单线读命令。
 * @param context 指向 Platform 长期持有的 QSPI Adapter Context。
 * @param instruction 由 W25Qxx Device 指定的 8-bit NOR Flash 命令。
 * @param data 接收命令返回数据的有效缓冲区。
 * @param data_length 本次命令读取的字节数，必须非零。
 * @retval W25Qxx 归一化后的总线状态。
 * @note  命令、数据都使用单线 SDR，且每次事务都重新发送指令。JEDEC ID 不含
 *        地址、交替字节和 dummy cycle；后续高速读取会由新的 W25Qxx BusOps
 *        明确表达其地址宽度、线数和时序，不能复用本函数偷偷改变语义。
 */
static W25Qxx_BusStatusTypeDef w25qxx_qspi_stm32_hal_read_command(
    void *context,
    uint8_t instruction,
    uint8_t *data,
    uint32_t data_length)
{
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter = context;
    QSPI_CommandTypeDef command = {0};
    HAL_StatusTypeDef hal_status;

    if ((adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (data == NULL) ||
        (data_length == 0u) ||
        (adapter->TimeoutMs == 0u))
    {
        return W25QXX_BUS_ERROR;
    }

    command.Instruction = instruction;
    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command.DummyCycles = 0u;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = data_length;
    command.DdrMode = QSPI_DDR_MODE_DISABLE;
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

    hal_status = HAL_QSPI_Command(adapter->Handle, &command, adapter->TimeoutMs);
    if (hal_status != HAL_OK)
    {
        return w25qxx_qspi_stm32_hal_map_status(hal_status);
    }

    return w25qxx_qspi_stm32_hal_map_status(
        HAL_QSPI_Receive(adapter->Handle, data, adapter->TimeoutMs));
}

/**
 * @brief 通过 HAL QSPI 间接模式执行一条由 Device 描述的带地址读命令。
 * @param context 指向 Platform 长期持有的 QSPI Adapter Context。
 * @param instruction 由 W25Qxx Device 指定的 8-bit NOR Flash 命令。
 * @param address 当前命令的物理地址。
 * @param transfer_config 由 Device 指定的地址、交替字节、dummy 和数据阶段。
 * @param data 接收命令返回数据的有效缓冲区。
 * @param data_length 本次命令读取的字节数，必须非零。
 * @retval W25Qxx 归一化后的总线状态。
 * @note  指令始终用单线 SDR；地址、交替字节、数据线数及 dummy cycle 严格按
 *        transfer_config 映射。当前支持 SFDP 的 1-1-1 和 W25Q256 0xEC 的
 *        1-4-4 读取。
 */
static W25Qxx_BusStatusTypeDef w25qxx_qspi_stm32_hal_read_addressed_command(
    void *context,
    uint8_t instruction,
    uint32_t address,
    const W25Qxx_BusAddressedTransferConfigTypeDef *transfer_config,
    uint8_t *data,
    uint32_t data_length)
{
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter = context;
    QSPI_CommandTypeDef command = {0};
    HAL_StatusTypeDef hal_status;

    if ((adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (transfer_config == NULL) ||
        (data == NULL) ||
        (data_length == 0u) ||
        (adapter->TimeoutMs == 0u))
    {
        return W25QXX_BUS_ERROR;
    }

    if (!w25qxx_qspi_stm32_hal_build_addressed_command(
            &command,
            instruction,
            address,
            transfer_config,
            data_length))
    {
        return W25QXX_BUS_ERROR;
    }

    hal_status = HAL_QSPI_Command(adapter->Handle, &command, adapter->TimeoutMs);
    if (hal_status != HAL_OK)
    {
        return w25qxx_qspi_stm32_hal_map_status(hal_status);
    }

    return w25qxx_qspi_stm32_hal_map_status(
        HAL_QSPI_Receive(adapter->Handle, data, adapter->TimeoutMs));
}

/**
 * @brief 通过 HAL QSPI 间接模式执行一条无地址、无数据的单线命令。
 * @param context 指向 Platform 长期持有的 QSPI Adapter Context。
 * @param instruction 由 W25Qxx Device 指定的 8-bit NOR Flash 命令。
 * @retval W25Qxx 归一化后的总线状态。
 * @note  首版只供启动期发送 Write Enable（0x06）。它不轮询 Flash，也不改变
 *        W25Qxx 的状态机语义。
 */
static W25Qxx_BusStatusTypeDef w25qxx_qspi_stm32_hal_execute_command(
    void *context,
    uint8_t instruction)
{
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter = context;
    QSPI_CommandTypeDef command = {0};

    if ((adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (adapter->TimeoutMs == 0u))
    {
        return W25QXX_BUS_ERROR;
    }

    command.Instruction = instruction;
    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command.DummyCycles = 0u;
    command.DataMode = QSPI_DATA_NONE;
    command.DdrMode = QSPI_DDR_MODE_DISABLE;
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

    return w25qxx_qspi_stm32_hal_map_status(
        HAL_QSPI_Command(adapter->Handle, &command, adapter->TimeoutMs));
}

/**
 * @brief 通过 HAL QSPI 间接模式执行一条由 Device 描述的带地址、无数据命令。
 * @param context 指向 Platform 长期持有的 QSPI Adapter Context。
 * @param instruction 由 W25Qxx Device 指定的 8-bit NOR Flash 命令。
 * @param address 当前命令的物理 Flash 地址。
 * @param transfer_config 由 Device 指定的地址、交替字节和 dummy 配置。
 * @retval W25Qxx 归一化后的总线状态。
 * @note 当前供 W25Q256 0x21 Sector Erase 使用：指令和 32-bit 地址均为单线，
 *       没有数据阶段。该函数只提交命令，不等待 Flash 内部擦除完成。
 */
static W25Qxx_BusStatusTypeDef w25qxx_qspi_stm32_hal_execute_addressed_command(
    void *context,
    uint8_t instruction,
    uint32_t address,
    const W25Qxx_BusAddressedTransferConfigTypeDef *transfer_config)
{
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter = context;
    QSPI_CommandTypeDef command = {0};

    if ((adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (transfer_config == NULL) ||
        (adapter->TimeoutMs == 0u))
    {
        return W25QXX_BUS_ERROR;
    }

    if (!w25qxx_qspi_stm32_hal_build_addressed_command(
            &command,
            instruction,
            address,
            transfer_config,
            0u))
    {
        return W25QXX_BUS_ERROR;
    }

    return w25qxx_qspi_stm32_hal_map_status(
        HAL_QSPI_Command(adapter->Handle, &command, adapter->TimeoutMs));
}

/**
 * @brief 通过 HAL QSPI 间接模式执行一条无地址的单线写命令。
 * @param context 指向 Platform 长期持有的 QSPI Adapter Context。
 * @param instruction 由 W25Qxx Device 指定的 8-bit NOR Flash 命令。
 * @param data 待发送的有效数据缓冲区。
 * @param data_length 本次命令写入的字节数，必须非零。
 * @retval W25Qxx 归一化后的总线状态。
 * @note  首版只供 Write Status Register-2（0x31）的单字节写入。HAL 的 Transmit
 *        原型未声明 const，但它仅消费数据，故 Adapter 在这一处收敛 const 转换。
 */
static W25Qxx_BusStatusTypeDef w25qxx_qspi_stm32_hal_write_command(
    void *context,
    uint8_t instruction,
    const uint8_t *data,
    uint32_t data_length)
{
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter = context;
    QSPI_CommandTypeDef command = {0};
    HAL_StatusTypeDef hal_status;

    if ((adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (data == NULL) ||
        (data_length == 0u) ||
        (adapter->TimeoutMs == 0u))
    {
        return W25QXX_BUS_ERROR;
    }

    command.Instruction = instruction;
    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command.DummyCycles = 0u;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = data_length;
    command.DdrMode = QSPI_DDR_MODE_DISABLE;
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

    hal_status = HAL_QSPI_Command(adapter->Handle, &command, adapter->TimeoutMs);
    if (hal_status != HAL_OK)
    {
        return w25qxx_qspi_stm32_hal_map_status(hal_status);
    }

    return w25qxx_qspi_stm32_hal_map_status(
        HAL_QSPI_Transmit(adapter->Handle, (uint8_t *)data, adapter->TimeoutMs));
}

/**
 * @brief 通过 HAL QSPI 间接模式执行一条由 Device 描述的带地址写命令。
 * @param context 指向 Platform 长期持有的 QSPI Adapter Context。
 * @param instruction 由 W25Qxx Device 指定的 8-bit NOR Flash 命令。
 * @param address 当前命令的物理 Flash 地址。
 * @param transfer_config 由 Device 指定的地址、交替字节、dummy 和数据阶段。
 * @param data 待发送的有效数据缓冲区。
 * @param data_length 本次命令写入的非零字节数。
 * @retval W25Qxx 归一化后的总线状态。
 * @note 指令始终用单线 SDR。当前供 W25Q256 0x34 使用，即固定 32-bit 单线
 *       地址和四线数据；HAL_QSPI_Transmit() 的原型未标记 const，Adapter 在
 *       唯一的边界处收敛该转换，不把 HAL 可变指针泄漏回 Component。
 */
static W25Qxx_BusStatusTypeDef w25qxx_qspi_stm32_hal_write_addressed_command(
    void *context,
    uint8_t instruction,
    uint32_t address,
    const W25Qxx_BusAddressedTransferConfigTypeDef *transfer_config,
    const uint8_t *data,
    uint32_t data_length)
{
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter = context;
    QSPI_CommandTypeDef command = {0};
    HAL_StatusTypeDef hal_status;

    if ((adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (transfer_config == NULL) ||
        (data == NULL) ||
        (data_length == 0u) ||
        (adapter->TimeoutMs == 0u))
    {
        return W25QXX_BUS_ERROR;
    }

    if (!w25qxx_qspi_stm32_hal_build_addressed_command(
            &command,
            instruction,
            address,
            transfer_config,
            data_length))
    {
        return W25QXX_BUS_ERROR;
    }

    hal_status = HAL_QSPI_Command(adapter->Handle, &command, adapter->TimeoutMs);
    if (hal_status != HAL_OK)
    {
        return w25qxx_qspi_stm32_hal_map_status(hal_status);
    }

    return w25qxx_qspi_stm32_hal_map_status(
        HAL_QSPI_Transmit(adapter->Handle, (uint8_t *)data, adapter->TimeoutMs));
}

/**
 * @brief 提供 W25Qxx 有界状态轮询所需的毫秒时间源。
 * @param context 未使用；保留以符合 W25Qxx BusOps 签名。
 * @retval HAL 基准毫秒计数。
 * @note  CubeMX 的 HAL Tick 在 Platform 初始化前已可用。该回调供启动期 QE
 *        轮询和运行期页编程超时判断共用；它不是延时函数，不等待也不访问 QSPI。
 */
static uint32_t w25qxx_qspi_stm32_hal_get_tick_ms(void *context)
{
    (void)context;
    return HAL_GetTick();
}

/** @brief W25Qxx Device 使用的 STM32 HAL QSPI BusOps 表。 */
static const W25Qxx_BusOpsTypeDef w25qxx_qspi_stm32_hal_ops = {
    .ReadCommand = w25qxx_qspi_stm32_hal_read_command,
    .ReadAddressedCommand = w25qxx_qspi_stm32_hal_read_addressed_command,
    .ExecuteCommand = w25qxx_qspi_stm32_hal_execute_command,
    .ExecuteAddressedCommand = w25qxx_qspi_stm32_hal_execute_addressed_command,
    .WriteCommand = w25qxx_qspi_stm32_hal_write_command,
    .WriteAddressedCommand = w25qxx_qspi_stm32_hal_write_addressed_command,
    .GetTickMs = w25qxx_qspi_stm32_hal_get_tick_ms
};

/**
 * @brief  将 STM32 HAL QSPI Context 绑定到 W25Qxx Device。
 * @param  hflash 待绑定的 W25Qxx Device Handle。
 * @param  adapter 由 Platform 长期持有的 HAL QSPI Adapter Context。
 * @retval W25QXX_OK 绑定成功。
 * @retval W25QXX_ERROR Handle、Adapter、QSPI Handle 或超时参数无效。
 * @note   本函数只完成依赖装配，不访问 QSPI 寄存器或 Flash。CubeMX 必须已在
 *         app_init() 之前成功执行 MX_QUADSPI_Init()；W25Qxx_Init() 才会发出
 *         实际 JEDEC ID 命令。
 */
W25Qxx_StatusTypeDef W25Qxx_QSPI_STM32HALAdapter_Bind(
    W25Qxx_HandleTypeDef *hflash,
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter)
{
    if ((hflash == NULL) ||
        (adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (adapter->TimeoutMs == 0u))
    {
        return W25QXX_ERROR;
    }

    hflash->BusOps = &w25qxx_qspi_stm32_hal_ops;
    hflash->BusContext = adapter;
    hflash->State = W25QXX_STATE_RESET;
    hflash->ErrorCode = W25QXX_ERROR_NONE;
    hflash->LastBusStatus = W25QXX_BUS_OK;
    hflash->ActiveOperation = W25QXX_OPERATION_NONE;
    hflash->ActiveOperationStartTickMs = 0u;
    return W25QXX_OK;
}
