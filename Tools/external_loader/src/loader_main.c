/**
 * @file loader_main.c
 * @brief STM32CubeProgrammer 调用的 W25Q256 外部烧录算法入口。
 */

#include <stdint.h>

#include "stm32h7xx_hal.h"

#include "loader_geometry.h"
#include "loader_w25q256.h"
#include "loader_w25q256_config.h"

static volatile uint32_t loader_tick;
static uint8_t cycle_counter_ready;
static uint32_t cycle_counter_last;
static uint32_t cycle_counter_cycles_per_ms;
static uint64_t cycle_counter_total;

static int loader_configure_system_clock(void);
static void loader_enable_cycle_counter(uint32_t cycles_per_ms);
static int loader_restore_memory_mapped_mode(void);
static uint32_t loader_checksum(uint32_t memory_address, uint32_t size, uint32_t initial_value);

/**
 * @brief 配置本板 25 MHz HSE、480 MHz 内核和 240 MHz D1HCLK。
 * @return 1 表示时钟配置完成；0 表示任一 HAL 时钟配置失败。
 */
static int loader_configure_system_clock(void)
{
    RCC_OscInitTypeDef oscillator_config = {0};
    RCC_ClkInitTypeDef clock_config = {0};

    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while (__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY) == 0U)
    {
    }

    oscillator_config.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator_config.HSEState = RCC_HSE_ON;
    oscillator_config.PLL.PLLState = RCC_PLL_ON;
    oscillator_config.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator_config.PLL.PLLM = 5U;
    oscillator_config.PLL.PLLN = 192U;
    oscillator_config.PLL.PLLP = 2U;
    oscillator_config.PLL.PLLQ = 20U;
    oscillator_config.PLL.PLLR = 6U;
    oscillator_config.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
    oscillator_config.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    oscillator_config.PLL.PLLFRACN = 0U;
    if (HAL_RCC_OscConfig(&oscillator_config) != HAL_OK)
    {
        return 0;
    }

    clock_config.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                             RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                             RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    clock_config.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock_config.SYSCLKDivider = RCC_SYSCLK_DIV1;
    clock_config.AHBCLKDivider = RCC_HCLK_DIV2;
    clock_config.APB3CLKDivider = RCC_APB3_DIV2;
    clock_config.APB1CLKDivider = RCC_APB1_DIV2;
    clock_config.APB2CLKDivider = RCC_APB2_DIV2;
    clock_config.APB4CLKDivider = RCC_APB4_DIV2;

    return (HAL_RCC_ClockConfig(&clock_config, FLASH_LATENCY_4) == HAL_OK) ? 1 : 0;
}

/**
 * @brief 按当前内核频率启用 DWT 周期计数器，为 HAL 轮询提供真实毫秒时基。
 * @param[in] cycles_per_ms 当前 Cortex-M7 内核每毫秒执行的周期数。
 * @return 无返回值。
 */
static void loader_enable_cycle_counter(uint32_t cycles_per_ms)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    cycle_counter_last = 0U;
    cycle_counter_cycles_per_ms = cycles_per_ms;
    cycle_counter_total = 0ULL;
    cycle_counter_ready = 1U;
}

/**
 * @brief 恢复映射窗口，使 CubeProgrammer 的校验回调可读取外部 Flash。
 * @return 1 表示映射成功；0 表示映射配置失败。
 */
static int loader_restore_memory_mapped_mode(void)
{
    return Loader_W25Q256_EnableMemoryMappedMode();
}

/**
 * @brief 逐字节计算 CubeProgrammer 所需的加和校验值。
 * @param[in] memory_address 已映射的外部 Flash 起始地址。
 * @param[in] size 参与校验的字节数。
 * @param[in] initial_value 初始校验值。
 * @return 32 位字节加和结果。
 */
static uint32_t loader_checksum(uint32_t memory_address, uint32_t size, uint32_t initial_value)
{
    volatile const uint8_t *memory = (volatile const uint8_t *)memory_address;

    while (size != 0U)
    {
        initial_value += *memory;
        ++memory;
        --size;
    }

    return initial_value;
}

/**
 * @brief 覆盖 HAL tick 初始化，loader 不依赖 SysTick 中断。
 * @param[in] tick_priority 未使用的 tick 优先级。
 * @retval HAL_OK 始终成功。
 */
HAL_StatusTypeDef HAL_InitTick(uint32_t tick_priority)
{
    (void)tick_priority;
    return HAL_OK;
}

/**
 * @brief 提供轮询超时所需的单调软件 tick。
 * @return 每次调用递增的 tick 值；仅用于 loader 内的有限等待。
 */
uint32_t HAL_GetTick(void)
{
    if (cycle_counter_ready != 0U)
    {
        const uint32_t cycle_counter_now = DWT->CYCCNT;

        /* 内核切换时钟后会重新设置换算基准，避免 HAL 轮询发生虚假超时。 */
        cycle_counter_total += (uint32_t)(cycle_counter_now - cycle_counter_last);
        cycle_counter_last = cycle_counter_now;
        return (uint32_t)(cycle_counter_total / cycle_counter_cycles_per_ms);
    }

    return loader_tick++;
}

/**
 * @brief 提供复位后 Flash 稳定所需的短延时。
 * @param[in] delay 近似等待 tick 数，不依赖中断时基。
 * @return 无返回值。
 */
void HAL_Delay(uint32_t delay)
{
    const uint32_t start_tick = HAL_GetTick();

    while ((HAL_GetTick() - start_tick) < delay)
    {
    }
}

/**
 * @brief 初始化外部烧录算法并开放 W25Q256 映射窗口。
 * @return 1 表示 CubeProgrammer 可访问该 32 MiB 外部 NOR；0 表示初始化失败。
 */
int Init(void)
{
    loader_tick = 0U;
    cycle_counter_ready = 0U;
    cycle_counter_last = 0U;
    cycle_counter_cycles_per_ms = 0U;
    cycle_counter_total = 0ULL;
    SystemInit();

    /* SystemInit 返回后使用 64 MHz HSI；先建立真实 tick，再调用带超时的 HAL。 */
    loader_enable_cycle_counter(64000U);
    HAL_Init();

    if (loader_configure_system_clock() == 0)
    {
        return 0;
    }

    /* PLL 切换完成后 Cortex-M7 为 480 MHz，重新建立毫秒换算基准。 */
    loader_enable_cycle_counter(480000U);
    if (Loader_W25Q256_Initialize() == 0)
    {
        return 0;
    }

    return loader_restore_memory_mapped_mode();
}

/**
 * @brief 按 W25Q256 页边界写入 CubeProgrammer 提供的缓冲区。
 * @param[in] address 以 0x90000000 为起点的目标地址。
 * @param[in] size 待写字节数。
 * @param[in] buffer 源数据缓冲区；调用期间由 CubeProgrammer 保持有效。
 * @return 1 表示写入完成且映射已恢复；0 表示地址、擦写或映射失败。
 */
int Write(uint32_t address, uint32_t size, uint8_t *buffer)
{
    uint32_t offset;

    if ((Loader_GeometryToOffset(address, size, &offset) == 0) || (buffer == NULL) ||
        (Loader_W25Q256_DisableMemoryMappedMode() == 0))
    {
        return 0;
    }

    if (Loader_W25Q256_Program(offset, buffer, size) == 0)
    {
        return 0;
    }

    return loader_restore_memory_mapped_mode();
}

/**
 * @brief 擦除覆盖 CubeProgrammer 给定闭区间的全部 4 KiB 扇区。
 * @param[in] erase_start_address 映射窗口内的擦除起始地址。
 * @param[in] erase_end_address 映射窗口内的擦除结束地址（含）。
 * @return 1 表示所有覆盖扇区均已擦除且映射已恢复；0 表示范围或操作失败。
 */
int SectorErase(uint32_t erase_start_address, uint32_t erase_end_address)
{
    uint32_t start_offset;
    uint32_t end_offset;
    uint32_t current_offset;

    if ((erase_end_address < erase_start_address) ||
        (Loader_GeometryToOffset(erase_start_address, 1U, &start_offset) == 0) ||
        (Loader_GeometryToOffset(erase_end_address, 1U, &end_offset) == 0) ||
        (Loader_W25Q256_DisableMemoryMappedMode() == 0))
    {
        return 0;
    }

    current_offset = start_offset & ~(LOADER_W25Q256_SECTOR_SIZE - 1U);
    end_offset &= ~(LOADER_W25Q256_SECTOR_SIZE - 1U);
    do
    {
        if (Loader_W25Q256_EraseSector(current_offset) == 0)
        {
            return 0;
        }
        current_offset += LOADER_W25Q256_SECTOR_SIZE;
    } while (current_offset <= end_offset);

    return loader_restore_memory_mapped_mode();
}

/**
 * @brief 擦除整片 32 MiB W25Q256。
 * @param[in] parallelism CubeProgrammer 兼容参数，W25Q256 不使用。
 * @return 1 表示整片擦除完成且映射已恢复；0 表示擦除或映射失败。
 */
int MassErase(uint32_t parallelism)
{
    (void)parallelism;

    if (Loader_W25Q256_DisableMemoryMappedMode() == 0)
    {
        return 0;
    }

    if (Loader_W25Q256_EraseChip() == 0)
    {
        return 0;
    }

    return loader_restore_memory_mapped_mode();
}

/**
 * @brief 计算外部 Flash 指定字节范围的加和校验值。
 * @param[in] start_address 映射窗口内的起始地址。
 * @param[in] size 待校验字节数。
 * @param[in] initial_value 初始校验值。
 * @return 加和校验结果；非法范围时返回 initial_value。
 */
uint32_t CheckSum(uint32_t start_address, uint32_t size, uint32_t initial_value)
{
    uint32_t offset;

    if ((Loader_GeometryToOffset(start_address, size, &offset) == 0) ||
        (loader_restore_memory_mapped_mode() == 0))
    {
        return initial_value;
    }

    return loader_checksum(start_address, size, initial_value);
}

/**
 * @brief 比较外部 Flash 与 RAM 缓冲区，并返回 CubeProgrammer ABI 规定的结果。
 * @param[in] memory_address 映射窗口内的 Flash 地址。
 * @param[in] ram_buffer_address CubeProgrammer 下载到 RAM 的源缓冲区地址。
 * @param[in] size 待比较的 32 位字数量。
 * @param[in] missalignment 低 4 位为首端跳过字节，高 16 位低 4 位为尾端跳过字节。
 * @return 高 32 位为加和校验；低 32 位为首个不匹配的 Flash 地址，成功时为零。
 */
uint64_t Verify(uint32_t memory_address,
                uint32_t ram_buffer_address,
                uint32_t size,
                uint32_t missalignment)
{
    uint32_t offset;
    uint32_t byte_count;
    uint32_t first_skip;
    uint32_t last_skip;
    uint32_t checksum;
    uint32_t index;
    volatile const uint8_t *memory;
    const uint8_t *ram_buffer;

    if ((size > (UINT32_MAX / 4U)) ||
        (loader_restore_memory_mapped_mode() == 0))
    {
        return 0ULL;
    }

    byte_count = size * 4U;
    first_skip = missalignment & 0xFU;
    last_skip = (missalignment >> 16U) & 0xFU;
    if ((byte_count < first_skip) || ((byte_count - first_skip) < last_skip))
    {
        return 0ULL;
    }

    memory_address += first_skip;
    byte_count -= first_skip + last_skip;
    if (Loader_GeometryToOffset(memory_address, byte_count, &offset) == 0)
    {
        return 0ULL;
    }

    checksum = loader_checksum(memory_address, byte_count, 0U);
    memory = (volatile const uint8_t *)memory_address;
    ram_buffer = (const uint8_t *)(ram_buffer_address + first_skip);
    for (index = 0U; index < byte_count; ++index)
    {
        if (memory[index] != ram_buffer[index])
        {
            return ((uint64_t)checksum << 32U) | (memory_address + index);
        }
    }

    return ((uint64_t)checksum << 32U);
}
