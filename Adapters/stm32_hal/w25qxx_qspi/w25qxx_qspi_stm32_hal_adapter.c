/**
  ******************************************************************************
  * @file    w25qxx_qspi_stm32_hal_adapter.c
  * @brief   STM32 HAL QSPI 到 W25Qxx BusOps 的同步 Adapter 实现。
  *
  * @details
  *          本 Module 把 W25Qxx Device 发出的无地址单线命令读取映射为 HAL QSPI
  *          间接模式的 Command + Receive 事务。当前只服务 JEDEC ID 识别，不使用
  *          DMA、自动状态轮询、内存映射或 QSPI IRQ 回调。
  ******************************************************************************
  */

#include "Adapters/stm32_hal/w25qxx_qspi/w25qxx_qspi_stm32_hal_adapter.h"

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

/** @brief W25Qxx Device 使用的 STM32 HAL QSPI BusOps 表。 */
static const W25Qxx_BusOpsTypeDef w25qxx_qspi_stm32_hal_ops = {
    .ReadCommand = w25qxx_qspi_stm32_hal_read_command
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
    return W25QXX_OK;
}
