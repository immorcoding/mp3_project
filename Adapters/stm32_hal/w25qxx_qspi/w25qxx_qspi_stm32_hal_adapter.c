/**
  ******************************************************************************
  * @file    w25qxx_qspi_stm32_hal_adapter.c
 * @brief   STM32 HAL QSPI 到 W25Qxx BusOps 的 Adapter 实现。
  *
  * @details
 *          本 Module 把 W25Qxx Device 发出的控制、读写命令映射为 HAL QSPI
 *          间接模式事务。当前服务 JEDEC ID、SFDP、状态寄存器、启动期 QE 配置、
 *          0xEC Quad I/O 同步读取和 MDMA 非阻塞读取、0x34 Quad 页数据传输，
 *          以及 WIP 清零的硬件自动状态轮询。Adapter 不持有 Task；QSPI IRQ
 *          只更新异步结果，读取完成后的 D-Cache 失效由 W25Qxx_Process() 所在
 *          普通上下文执行。
  ******************************************************************************
  */

#include "Adapters/stm32_hal/w25qxx_qspi/w25qxx_qspi_stm32_hal_adapter.h"
#include "Adapters/cortex/cache/cortex_m7_dcache_adapter.h"

#include <stdbool.h>
#include <stddef.h>

/* 120 MHz QSPI 下约 0.55 ms 的硬件轮询间隔，避免 WIP 期间占满串行总线。 */
#define W25QXX_QSPI_STM32_HAL_STATUS_POLL_INTERVAL_CYCLES  0xFFFFu

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
 * @brief 清除当前 Adapter 保存的 MDMA 读取元数据。
 * @param adapter 当前 STM32 HAL QSPI Adapter Context。
 * @note  该元数据不表示 W25Qxx Device 状态；Device 的 BUSY/READY/ERROR 转换
 *        仍完全由 Components/w25qxx 管理。
 */
static void w25qxx_qspi_stm32_hal_clear_dma_read_metadata(
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter)
{
    adapter->DMAReadBuffer = NULL;
    adapter->DMAReadByteCount = 0u;
    adapter->DMAReadPending = false;
}

/**
 * @brief 清除当前 Adapter 保存的自动状态轮询元数据。
 * @param adapter 当前 STM32 HAL QSPI Adapter Context。
 * @note 该函数不改变 W25Qxx Device 状态；Device 的 BUSY/READY/ERROR 转换由
 *       W25Qxx_Process() 在普通上下文完成。
 */
static void w25qxx_qspi_stm32_hal_clear_status_polling_metadata(
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter)
{
    adapter->StatusPollingPending = false;
}

/**
 * @brief 在 MDMA 读取完成后使 CPU 重新从 AXI SRAM 读取新数据。
 * @param adapter 保存本次读取 DMA 缓冲区和长度的 Adapter Context。
 * @retval true 接收缓冲区 Cache 已成功失效，或当前没有待处理读取。
 * @retval false 元数据不完整或 Cache line 约束不满足。
 * @note 只能在普通任务上下文、确认 QSPI 接收阶段完成后调用；IRQ 不得调用。
 */
static bool w25qxx_qspi_stm32_hal_finish_dma_read(
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter)
{
    if (!adapter->DMAReadPending)
    {
        return true;
    }

    if ((adapter->DMAReadBuffer == NULL) || (adapter->DMAReadByteCount == 0u))
    {
        return false;
    }

    return CortexM7DCache_Invalidate_Aligned(adapter->DMAReadBuffer,
                                              adapter->DMAReadByteCount);
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
 * @brief 通过 HAL QSPI 间接模式和已绑定 MDMA 启动一条带地址非阻塞读取命令。
 * @param context 指向 Platform 长期持有的 QSPI Adapter Context。
 * @param instruction 由 W25Qxx Device 指定的 8-bit NOR Flash 命令。
 * @param address 当前命令的物理地址。
 * @param transfer_config 由 Device 指定的地址、交替字节、dummy 和数据阶段。
 * @param data 接收 MDMA 数据的有效缓冲区。
 * @param data_length 本次命令读取的字节数，必须非零。
 * @retval W25Qxx 归一化后的立即启动状态。
 * @note  当前 QSPI 的 `hmdma` 由 CubeMX 使用 `QUADSPI_FIFO_TH` 请求绑定。
 *        读取前先 Clean+Invalidate 目标范围，防止写回脏 Cache line 覆盖 MDMA
 *        新数据；读取后的 Invalidate 则延后到普通上下文的状态查询。缓冲区必须
 *        32-byte 对齐且长度为完整 Cache line，否则 Cache Adapter 会拒绝启动。
 */
static W25Qxx_BusStatusTypeDef w25qxx_qspi_stm32_hal_start_read_addressed_command(
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
        (adapter->Handle->hmdma == NULL) ||
        (transfer_config == NULL) ||
        (data == NULL) ||
        (data_length == 0u) ||
        (adapter->TimeoutMs == 0u) ||
        adapter->DMAReadPending ||
        adapter->StatusPollingPending)
    {
        return (adapter != NULL) &&
                       (adapter->DMAReadPending || adapter->StatusPollingPending) ?
                   W25QXX_BUS_BUSY :
                   W25QXX_BUS_ERROR;
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

    if (!CortexM7DCache_CleanInvalidate_Aligned(data, data_length))
    {
        return W25QXX_BUS_ERROR;
    }

    adapter->DMAReadBuffer = data;
    adapter->DMAReadByteCount = data_length;
    adapter->DMAReadPending = true;
    adapter->DMAReadStatus = W25QXX_BUS_BUSY;
    /* QSPI IRQ 可能在 HAL_QSPI_Receive_DMA() 返回前到达，先发布完整元数据。 */
    __DMB();

    hal_status = HAL_QSPI_Command(adapter->Handle, &command, adapter->TimeoutMs);
    if (hal_status == HAL_OK)
    {
        hal_status = HAL_QSPI_Receive_DMA(adapter->Handle, data);
    }

    if (hal_status != HAL_OK)
    {
        w25qxx_qspi_stm32_hal_clear_dma_read_metadata(adapter);
        adapter->DMAReadStatus = w25qxx_qspi_stm32_hal_map_status(hal_status);
        __DMB();
    }

    return w25qxx_qspi_stm32_hal_map_status(hal_status);
}

/**
 * @brief 查询当前 QSPI MDMA 带地址读取的完成状态并在完成后收尾 Cache。
 * @param context 指向 Platform 长期持有的 QSPI Adapter Context。
 * @retval W25QXX_BUS_BUSY 尚未收到 QSPI 完成或错误事件。
 * @retval W25QXX_BUS_OK 已接收全部数据并完成 D-Cache Invalidate。
 * @retval 其他值 IRQ 报错、元数据异常或 Cache 收尾失败。
 * @note 本函数不等待；它应由接收完成任务通知唤醒的普通上下文调用。若调用时
 *       尚未收到事件，直接返回 BUSY，供上层的有界超时状态机处理。
 */
static W25Qxx_BusStatusTypeDef
    w25qxx_qspi_stm32_hal_get_read_addressed_command_status(void *context)
{
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter = context;
    W25Qxx_BusStatusTypeDef status;

    if (adapter == NULL)
    {
        return W25QXX_BUS_ERROR;
    }

    status = adapter->DMAReadStatus;
    __DMB();
    if (status == W25QXX_BUS_BUSY)
    {
        return W25QXX_BUS_BUSY;
    }

    if (status != W25QXX_BUS_OK)
    {
        w25qxx_qspi_stm32_hal_clear_dma_read_metadata(adapter);
        return status;
    }

    if (!w25qxx_qspi_stm32_hal_finish_dma_read(adapter))
    {
        w25qxx_qspi_stm32_hal_clear_dma_read_metadata(adapter);
        adapter->DMAReadStatus = W25QXX_BUS_ERROR;
        return W25QXX_BUS_ERROR;
    }

    w25qxx_qspi_stm32_hal_clear_dma_read_metadata(adapter);
    return W25QXX_BUS_OK;
}

/**
 * @brief 以 HAL QSPI 中断自动轮询等待 W25Qxx 指定状态位匹配。
 * @param context 指向 Platform 长期持有的 QSPI Adapter Context。
 * @param instruction 由 W25Qxx Device 指定的单线状态读取命令。
 * @param match 状态比较的目标值。
 * @param mask 状态比较掩码；当前 WIP 清零使用 0x01。
 * @retval W25QXX_BUS_OK QSPI 已进入自动轮询；Status Match 或错误将由 IRQ 更新。
 * @retval 其他值 参数无效、已有读取/轮询在飞或 HAL 拒绝启动。
 * @note 自动停止开启后，STM32 HAL 在 Status Match IRQ 到达前已把 QSPI Handle
 *       切回 READY。该函数不等待 tPP 或 tSE，也不读取状态寄存器的值。
 */
static W25Qxx_BusStatusTypeDef
    w25qxx_qspi_stm32_hal_start_status_match_polling(
        void *context,
        uint8_t instruction,
        uint8_t match,
        uint8_t mask)
{
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter = context;
    QSPI_CommandTypeDef command = {0};
    QSPI_AutoPollingTypeDef polling_config = {0};
    HAL_StatusTypeDef hal_status;

    if ((adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (adapter->TimeoutMs == 0u) ||
        adapter->DMAReadPending ||
        adapter->StatusPollingPending)
    {
        return ((adapter != NULL) &&
                (adapter->DMAReadPending || adapter->StatusPollingPending)) ?
                   W25QXX_BUS_BUSY :
                   W25QXX_BUS_ERROR;
    }

    command.Instruction = instruction;
    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command.DummyCycles = 0u;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = 1u;
    command.DdrMode = QSPI_DDR_MODE_DISABLE;
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

    polling_config.Match = match;
    polling_config.Mask = mask;
    polling_config.MatchMode = QSPI_MATCH_MODE_AND;
    polling_config.StatusBytesSize = 1u;
    polling_config.Interval = W25QXX_QSPI_STM32_HAL_STATUS_POLL_INTERVAL_CYCLES;
    polling_config.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;

    adapter->StatusPollingPending = true;
    adapter->StatusPollingStatus = W25QXX_BUS_BUSY;
    /* Status Match IRQ 可能在 HAL_QSPI_AutoPolling_IT() 返回前到达。 */
    __DMB();

    hal_status = HAL_QSPI_AutoPolling_IT(adapter->Handle,
                                         &command,
                                         &polling_config);
    if (hal_status != HAL_OK)
    {
        w25qxx_qspi_stm32_hal_clear_status_polling_metadata(adapter);
        adapter->StatusPollingStatus = w25qxx_qspi_stm32_hal_map_status(
            hal_status);
        __DMB();
    }

    return w25qxx_qspi_stm32_hal_map_status(hal_status);
}

/**
 * @brief 查询一次自动状态轮询的 IRQ 结果。
 * @param context 指向 Platform 长期持有的 QSPI Adapter Context。
 * @retval W25QXX_BUS_BUSY 尚未收到 Status Match 或错误 IRQ。
 * @retval W25QXX_BUS_OK WIP 已按 Device 指定条件匹配，QSPI 自动轮询已停止。
 * @retval 其他值 QSPI IRQ 报错或已中止。
 * @note 仅在普通上下文调用；不访问 Flash，也不等待。
 */
static W25Qxx_BusStatusTypeDef
    w25qxx_qspi_stm32_hal_get_status_match_polling_status(void *context)
{
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter = context;
    W25Qxx_BusStatusTypeDef status;

    if (adapter == NULL)
    {
        return W25QXX_BUS_ERROR;
    }

    status = adapter->StatusPollingStatus;
    __DMB();
    if (status == W25QXX_BUS_BUSY)
    {
        return W25QXX_BUS_BUSY;
    }

    w25qxx_qspi_stm32_hal_clear_status_polling_metadata(adapter);
    return status;
}

/**
 * @brief 中止一次仍未达到状态匹配条件的 QSPI 自动轮询。
 * @param context 指向 Platform 长期持有的 QSPI Adapter Context。
 * @retval W25QXX_BUS_OK QSPI 已回到 READY。
 * @retval 其他值 HAL 中止失败或参数无效。
 * @note 只在 W25Qxx 软件时限到期的错误路径调用。HAL_QSPI_Abort() 中止的是
 *       控制器轮询，并不会中止 NOR 内部已开始的页编程或扇区擦除。
 */
static W25Qxx_BusStatusTypeDef
    w25qxx_qspi_stm32_hal_abort_status_match_polling(void *context)
{
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter = context;
    HAL_StatusTypeDef hal_status;

    if ((adapter == NULL) || (adapter->Handle == NULL))
    {
        return W25QXX_BUS_ERROR;
    }

    hal_status = HAL_QSPI_Abort(adapter->Handle);
    w25qxx_qspi_stm32_hal_clear_status_polling_metadata(adapter);
    adapter->StatusPollingStatus = w25qxx_qspi_stm32_hal_map_status(hal_status);
    __DMB();
    return w25qxx_qspi_stm32_hal_map_status(hal_status);
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
    .StartReadAddressedCommand = w25qxx_qspi_stm32_hal_start_read_addressed_command,
    .GetReadAddressedCommandStatus =
        w25qxx_qspi_stm32_hal_get_read_addressed_command_status,
    .StartStatusMatchPolling =
        w25qxx_qspi_stm32_hal_start_status_match_polling,
    .GetStatusMatchPollingStatus =
        w25qxx_qspi_stm32_hal_get_status_match_polling_status,
    .AbortStatusMatchPolling =
        w25qxx_qspi_stm32_hal_abort_status_match_polling,
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
    w25qxx_qspi_stm32_hal_clear_dma_read_metadata(adapter);
    adapter->DMAReadStatus = W25QXX_BUS_OK;
    w25qxx_qspi_stm32_hal_clear_status_polling_metadata(adapter);
    adapter->StatusPollingStatus = W25QXX_BUS_OK;
    return W25QXX_OK;
}

/**
 * @brief 记录 HAL QSPI 间接读取已完成。
 * @param adapter 当前已绑定 QSPI Adapter Context。
 * @note 由 Platform 在 QSPI IRQ 上下文调用。此处只写入 volatile 完成状态，不做
 *        Cache 维护、日志、状态机推进或 RTOS 调用；普通上下文后续通过 BusOps
 *        状态查询完成 D-Cache Invalidate。
 */
void W25Qxx_QSPI_STM32HALAdapter_NotifyReadComplete(
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter)
{
    if ((adapter != NULL) && adapter->DMAReadPending)
    {
        adapter->DMAReadStatus = W25QXX_BUS_OK;
        __DMB();
    }
}

/**
 * @brief 记录 HAL QSPI 当前异步操作的错误或中止。
 * @param adapter 当前已绑定 QSPI Adapter Context。
 * @note 由 Platform 在 QSPI IRQ 上下文调用。中止或错误数据不得交给 CPU 消费，
 *        因而 MDMA 读取的后续状态查询只清理元数据并返回错误；自动状态轮询则
 *        在普通上下文把 W25Qxx Device 置 ERROR。
 */
void W25Qxx_QSPI_STM32HALAdapter_NotifyOperationError(
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter)
{
    if ((adapter != NULL) && adapter->DMAReadPending)
    {
        adapter->DMAReadStatus = W25QXX_BUS_ERROR;
        __DMB();
    }

    if ((adapter != NULL) && adapter->StatusPollingPending)
    {
        adapter->StatusPollingStatus = W25QXX_BUS_ERROR;
        __DMB();
    }
}

/**
 * @brief 记录 HAL QSPI 自动轮询已满足状态匹配条件。
 * @param adapter 当前已绑定 QSPI Adapter Context。
 * @note 由 Platform 在 Status Match IRQ 上下文调用。此处只写入 volatile 完成
 *       状态；不读取 SR1、不推进 W25Qxx 状态机，也不调用 RTOS。
 */
void W25Qxx_QSPI_STM32HALAdapter_NotifyStatusMatch(
    W25Qxx_QSPI_STM32HALAdapterTypeDef *adapter)
{
    if ((adapter != NULL) && adapter->StatusPollingPending)
    {
        adapter->StatusPollingStatus = W25QXX_BUS_OK;
        __DMB();
    }
}
