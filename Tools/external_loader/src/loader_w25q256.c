/**
 * @file loader_w25q256.c
 * @brief W25Q256 的同步 QSPI 擦写实现，供 CubeProgrammer loader 使用。
 */

#include <string.h>

#include "stm32h7xx_hal.h"

#include "loader_w25q256.h"
#include "loader_w25q256_config.h"

static QSPI_HandleTypeDef qspi_handle;
static uint8_t memory_mapped;

static void loader_prepare_command(QSPI_CommandTypeDef *command);
static int loader_send_no_data_command(uint8_t instruction);
static int loader_read_register(uint8_t instruction, uint8_t *value);
static int loader_wait_ready(uint32_t timeout_ticks);
static int loader_write_enable(void);
static int loader_reset_memory(void);
static int loader_validate_jedec_id(void);
static int loader_ensure_quad_enabled(void);

/**
 * @brief 初始化一个不带数据阶段的单线 QSPI 命令描述。
 * @param[out] command 待初始化的 HAL 命令对象。
 * @return 无返回值。
 */
static void loader_prepare_command(QSPI_CommandTypeDef *command)
{
    (void)memset(command, 0, sizeof(*command));
    command->InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command->AddressMode = QSPI_ADDRESS_NONE;
    command->AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command->DataMode = QSPI_DATA_NONE;
    command->DummyCycles = 0U;
    command->DdrMode = QSPI_DDR_MODE_DISABLE;
    command->DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command->SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
}

/**
 * @brief 发送没有地址和数据阶段的单线 Flash 指令。
 * @param[in] instruction 待发送的 W25Q256 指令码。
 * @retval 1 指令已由 QSPI 接受。
 * @retval 0 QSPI 命令阶段失败。
 */
static int loader_send_no_data_command(uint8_t instruction)
{
    QSPI_CommandTypeDef command;

    loader_prepare_command(&command);
    command.Instruction = instruction;

    return (HAL_QSPI_Command(&qspi_handle,
                             &command,
                             LOADER_W25Q256_COMMAND_TIMEOUT_TICKS) == HAL_OK)
               ? 1
               : 0;
}

/**
 * @brief 读取 W25Q256 的一个状态寄存器。
 * @param[in] instruction 读取寄存器的指令码。
 * @param[out] value 成功时写入寄存器内容。
 * @retval 1 读取成功。
 * @retval 0 参数为空或 QSPI 事务失败。
 */
static int loader_read_register(uint8_t instruction, uint8_t *value)
{
    QSPI_CommandTypeDef command;

    if (value == NULL)
    {
        return 0;
    }

    loader_prepare_command(&command);
    command.Instruction = instruction;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = 1U;

    if (HAL_QSPI_Command(&qspi_handle,
                         &command,
                         LOADER_W25Q256_COMMAND_TIMEOUT_TICKS) != HAL_OK)
    {
        return 0;
    }

    return (HAL_QSPI_Receive(&qspi_handle,
                             value,
                             LOADER_W25Q256_COMMAND_TIMEOUT_TICKS) == HAL_OK)
               ? 1
               : 0;
}

/**
 * @brief 轮询 SR1.WIP，等待已开始的 Flash 操作完成。
 * @param[in] timeout_ticks 最大轮询 tick 数。
 * @retval 1 WIP 已清零。
 * @retval 0 状态读取失败或超时。
 */
static int loader_wait_ready(uint32_t timeout_ticks)
{
    const uint32_t start_tick = HAL_GetTick();
    uint8_t status_register;

    do
    {
        if (loader_read_register(LOADER_W25Q256_COMMAND_READ_STATUS1,
                                 &status_register) == 0)
        {
            return 0;
        }

        if ((status_register & LOADER_W25Q256_STATUS1_WIP) == 0U)
        {
            return 1;
        }
    } while ((HAL_GetTick() - start_tick) <= timeout_ticks);

    return 0;
}

/**
 * @brief 请求 Flash 接受下一条会修改阵列或状态寄存器的命令。
 * @return 1 表示 WEL 已置位；0 表示命令、状态读取或核验失败。
 */
static int loader_write_enable(void)
{
    uint8_t status_register;

    if (loader_send_no_data_command(LOADER_W25Q256_COMMAND_WRITE_ENABLE) == 0)
    {
        return 0;
    }

    if (loader_read_register(LOADER_W25Q256_COMMAND_READ_STATUS1,
                             &status_register) == 0)
    {
        return 0;
    }

    return ((status_register & LOADER_W25Q256_STATUS1_WEL) != 0U) ? 1 : 0;
}

/**
 * @brief 将可能处于未知模式的 Flash 复位回默认 SPI 模式。
 * @return 1 表示复位后的 WIP 已清零；0 表示复位或等待失败。
 */
static int loader_reset_memory(void)
{
    if (loader_send_no_data_command(LOADER_W25Q256_COMMAND_RESET_ENABLE) == 0)
    {
        return 0;
    }

    if (loader_send_no_data_command(LOADER_W25Q256_COMMAND_RESET) == 0)
    {
        return 0;
    }

    HAL_Delay(1U);
    return loader_wait_ready(LOADER_W25Q256_COMMAND_TIMEOUT_TICKS);
}

/**
 * @brief 校验本板预期的 Winbond W25Q256 JEDEC 厂商码和容量码。
 * @return 1 表示识别为 W25Q256；0 表示读取失败或芯片型号不匹配。
 */
static int loader_validate_jedec_id(void)
{
    QSPI_CommandTypeDef command;
    uint8_t jedec_id[3];

    loader_prepare_command(&command);
    command.Instruction = LOADER_W25Q256_COMMAND_READ_JEDEC_ID;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = sizeof(jedec_id);

    if (HAL_QSPI_Command(&qspi_handle,
                         &command,
                         LOADER_W25Q256_COMMAND_TIMEOUT_TICKS) != HAL_OK)
    {
        return 0;
    }

    if (HAL_QSPI_Receive(&qspi_handle,
                         jedec_id,
                         LOADER_W25Q256_COMMAND_TIMEOUT_TICKS) != HAL_OK)
    {
        return 0;
    }

    return ((jedec_id[0] == LOADER_W25Q256_JEDEC_MANUFACTURER) &&
            (jedec_id[2] == LOADER_W25Q256_JEDEC_CAPACITY))
               ? 1
               : 0;
}

/**
 * @brief 确保 W25Q256 的 QE 位已打开，以允许四线读和页编程。
 * @return 1 表示 QE 已确认；0 表示状态读写或核验失败。
 */
static int loader_ensure_quad_enabled(void)
{
    QSPI_CommandTypeDef command;
    uint8_t status_register_2;

    if (loader_read_register(LOADER_W25Q256_COMMAND_READ_STATUS2,
                             &status_register_2) == 0)
    {
        return 0;
    }

    if ((status_register_2 & LOADER_W25Q256_STATUS2_QE) != 0U)
    {
        return 1;
    }

    if ((loader_wait_ready(LOADER_W25Q256_COMMAND_TIMEOUT_TICKS) == 0) ||
        (loader_write_enable() == 0))
    {
        return 0;
    }

    loader_prepare_command(&command);
    command.Instruction = LOADER_W25Q256_COMMAND_WRITE_STATUS2;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = 1U;
    status_register_2 |= LOADER_W25Q256_STATUS2_QE;

    if ((HAL_QSPI_Command(&qspi_handle,
                          &command,
                          LOADER_W25Q256_COMMAND_TIMEOUT_TICKS) != HAL_OK) ||
        (HAL_QSPI_Transmit(&qspi_handle,
                           &status_register_2,
                           LOADER_W25Q256_COMMAND_TIMEOUT_TICKS) != HAL_OK) ||
        (loader_wait_ready(LOADER_W25Q256_PROGRAM_TIMEOUT_TICKS) == 0) ||
        (loader_read_register(LOADER_W25Q256_COMMAND_READ_STATUS2,
                              &status_register_2) == 0))
    {
        return 0;
    }

    return ((status_register_2 & LOADER_W25Q256_STATUS2_QE) != 0U) ? 1 : 0;
}

/**
 * @brief 初始化本板 QSPI 外设并准备 W25Q256 直接擦写协议。
 * @return 1 表示外设、型号和 QE 均已就绪；0 表示初始化失败。
 */
int Loader_W25Q256_Initialize(void)
{
    (void)memset(&qspi_handle, 0, sizeof(qspi_handle));
    memory_mapped = 0U;

    qspi_handle.Instance = QUADSPI;
    qspi_handle.Init.ClockPrescaler = 1U;
    qspi_handle.Init.FifoThreshold = 16U;
    qspi_handle.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
    qspi_handle.Init.FlashSize = LOADER_W25Q256_FLASH_SIZE_POSITION;
    qspi_handle.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_6_CYCLE;
    qspi_handle.Init.ClockMode = QSPI_CLOCK_MODE_0;
    qspi_handle.Init.FlashID = QSPI_FLASH_ID_1;
    qspi_handle.Init.DualFlash = QSPI_DUALFLASH_DISABLE;

    if (HAL_QSPI_Init(&qspi_handle) != HAL_OK)
    {
        return 0;
    }

    return ((loader_reset_memory() != 0) &&
            (loader_validate_jedec_id() != 0) &&
            (loader_ensure_quad_enabled() != 0))
               ? 1
               : 0;
}

/**
 * @brief 将任意长度数据按 256 B 页边界写入 W25Q256。
 * @param[in] offset 起始物理偏移。
 * @param[in] data 待写数据，调用期间必须保持有效。
 * @param[in] size 待写字节数，必须非零。
 * @return 1 表示所有页均已完成 WIP 轮询；0 表示输入或 Flash 事务失败。
 */
int Loader_W25Q256_Program(uint32_t offset, const uint8_t *data, uint32_t size)
{
    QSPI_CommandTypeDef command;
    uint32_t page_remaining;
    uint32_t chunk_size;

    if ((data == NULL) || (size == 0U))
    {
        return 0;
    }

    while (size != 0U)
    {
        page_remaining = LOADER_W25Q256_PAGE_SIZE -
                         (offset & (LOADER_W25Q256_PAGE_SIZE - 1U));
        chunk_size = (size < page_remaining) ? size : page_remaining;

        if ((loader_wait_ready(LOADER_W25Q256_PROGRAM_TIMEOUT_TICKS) == 0) ||
            (loader_write_enable() == 0))
        {
            return 0;
        }

        loader_prepare_command(&command);
        command.Instruction = LOADER_W25Q256_COMMAND_PAGE_PROGRAM_4BYTE;
        command.AddressMode = QSPI_ADDRESS_1_LINE;
        command.AddressSize = QSPI_ADDRESS_32_BITS;
        command.Address = offset;
        command.DataMode = QSPI_DATA_4_LINES;
        command.NbData = chunk_size;

        if ((HAL_QSPI_Command(&qspi_handle,
                              &command,
                              LOADER_W25Q256_COMMAND_TIMEOUT_TICKS) != HAL_OK) ||
            (HAL_QSPI_Transmit(&qspi_handle,
                               (uint8_t *)data,
                               LOADER_W25Q256_COMMAND_TIMEOUT_TICKS) != HAL_OK) ||
            (loader_wait_ready(LOADER_W25Q256_PROGRAM_TIMEOUT_TICKS) == 0))
        {
            return 0;
        }

        offset += chunk_size;
        data += chunk_size;
        size -= chunk_size;
    }

    return 1;
}

/**
 * @brief 擦除一个 4 KiB 对齐的 W25Q256 扇区。
 * @param[in] offset 待擦除扇区的物理偏移，必须 4 KiB 对齐。
 * @return 1 表示擦除完成；0 表示对齐、命令或 WIP 等待失败。
 */
int Loader_W25Q256_EraseSector(uint32_t offset)
{
    QSPI_CommandTypeDef command;

    if ((offset & (LOADER_W25Q256_SECTOR_SIZE - 1U)) != 0U)
    {
        return 0;
    }

    if ((loader_wait_ready(LOADER_W25Q256_ERASE_TIMEOUT_TICKS) == 0) ||
        (loader_write_enable() == 0))
    {
        return 0;
    }

    loader_prepare_command(&command);
    command.Instruction = LOADER_W25Q256_COMMAND_SECTOR_ERASE_4BYTE;
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.AddressSize = QSPI_ADDRESS_32_BITS;
    command.Address = offset;

    if (HAL_QSPI_Command(&qspi_handle,
                         &command,
                         LOADER_W25Q256_COMMAND_TIMEOUT_TICKS) != HAL_OK)
    {
        return 0;
    }

    return loader_wait_ready(LOADER_W25Q256_ERASE_TIMEOUT_TICKS);
}

/**
 * @brief 擦除整片 W25Q256；该操作可能持续数分钟。
 * @return 1 表示整片擦除完成；0 表示命令或完成等待失败。
 */
int Loader_W25Q256_EraseChip(void)
{
    if ((loader_wait_ready(LOADER_W25Q256_ERASE_TIMEOUT_TICKS) == 0) ||
        (loader_write_enable() == 0) ||
        (loader_send_no_data_command(LOADER_W25Q256_COMMAND_CHIP_ERASE) == 0))
    {
        return 0;
    }

    return loader_wait_ready(LOADER_W25Q256_CHIP_TIMEOUT_TICKS);
}

/**
 * @brief 开启 0x90000000 的 W25Q256 只读内存映射窗口。
 * @return 1 表示映射已开启；0 表示 QSPI 配置失败。
 */
int Loader_W25Q256_EnableMemoryMappedMode(void)
{
    QSPI_CommandTypeDef command;
    QSPI_MemoryMappedTypeDef memory_mapped_config;

    if (memory_mapped != 0U)
    {
        return 1;
    }

    loader_prepare_command(&command);
    command.Instruction = LOADER_W25Q256_COMMAND_READ_4BYTE_QUAD_IO;
    command.AddressMode = QSPI_ADDRESS_4_LINES;
    command.AddressSize = QSPI_ADDRESS_32_BITS;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_4_LINES;
    command.AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
    command.AlternateBytes = 0xFFU;
    command.DataMode = QSPI_DATA_4_LINES;
    command.DummyCycles = 4U;

    memory_mapped_config.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;
    memory_mapped_config.TimeOutPeriod = 0U;

    if (HAL_QSPI_MemoryMapped(&qspi_handle, &command, &memory_mapped_config) != HAL_OK)
    {
        return 0;
    }

    memory_mapped = 1U;
    return 1;
}

/**
 * @brief 退出只读内存映射，使 QSPI 可执行间接擦写命令。
 * @return 1 表示已处于间接模式；0 表示 HAL 中止映射失败。
 */
int Loader_W25Q256_DisableMemoryMappedMode(void)
{
    if (memory_mapped == 0U)
    {
        return 1;
    }

    if (HAL_QSPI_Abort(&qspi_handle) != HAL_OK)
    {
        return 0;
    }

    memory_mapped = 0U;
    return 1;
}

/**
 * @brief 配置本板 QSPI 时钟、GPIO 和外设复位序列。
 * @param[in] handle 待初始化的 QSPI Handle，仅支持 QUADSPI。
 * @return 无返回值；HAL 初始化失败会由调用方报告。
 */
void HAL_QSPI_MspInit(QSPI_HandleTypeDef *handle)
{
    GPIO_InitTypeDef gpio_init;
    RCC_PeriphCLKInitTypeDef peripheral_clock;

    if ((handle == NULL) || (handle->Instance != QUADSPI))
    {
        return;
    }

    (void)memset(&peripheral_clock, 0, sizeof(peripheral_clock));
    peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_QSPI;
    peripheral_clock.QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
    {
        return;
    }

    __HAL_RCC_QSPI_CLK_ENABLE();
    __HAL_RCC_QSPI_FORCE_RESET();
    __HAL_RCC_QSPI_RELEASE_RESET();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    (void)memset(&gpio_init, 0, sizeof(gpio_init));
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    gpio_init.Pin = GPIO_PIN_2;
    gpio_init.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOE, &gpio_init);

    gpio_init.Pin = GPIO_PIN_6 | GPIO_PIN_10;
    gpio_init.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOF, &gpio_init);

    gpio_init.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    gpio_init.Alternate = GPIO_AF10_QUADSPI;
    HAL_GPIO_Init(GPIOF, &gpio_init);

    gpio_init.Pin = GPIO_PIN_6;
    gpio_init.Alternate = GPIO_AF10_QUADSPI;
    HAL_GPIO_Init(GPIOG, &gpio_init);
}
