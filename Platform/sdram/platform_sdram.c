/**
  ******************************************************************************
  * @file    platform_sdram.c
  * @brief   当前板载 32 MiB x16 SDRAM 的初始化和硬件诊断实现。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "Platform/sdram/platform_sdram.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "Adapters/cortex/cache/cortex_m7_dcache_adapter.h"
#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Platform/sdram/platform_sdram_config.h"

#include "Core/Inc/fmc.h"

/* Private variables ---------------------------------------------------------*/
/** @brief 标识 JEDEC 初始化序列已成功完成，避免重复重置正在使用的 SDRAM。 */
static bool platform_sdram_initialized;

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  发送一条面向 FMC SDRAM Bank1 的 JEDEC 初始化命令。
  * @param  command_mode HAL 定义的 SDRAM 命令类型。
  * @param  auto_refresh_count 本条命令要求的 AUTO REFRESH 次数。
  * @param  mode_register_value LOAD MODE 命令使用的模式寄存器值。
  * @retval true 命令已由 HAL 接受。
  * @retval false HAL 拒绝命令或发生超时。
  */
static bool platform_sdram_send_command(uint32_t command_mode,
                                        uint32_t auto_refresh_count,
                                        uint32_t mode_register_value)
{
    FMC_SDRAM_CommandTypeDef command = {0};

    command.CommandMode = command_mode;
    command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command.AutoRefreshNumber = auto_refresh_count;
    command.ModeRegisterDefinition = mode_register_value;

    return HAL_SDRAM_SendCommand(&hsdram1, &command, HAL_MAX_DELAY) == HAL_OK;
}

/**
  * @brief  把包含给定地址的 D-Cache line 写回 SDRAM 后再失效。
  * @param  address 位于 SDRAM 的字地址。
  * @note   该辅助函数只用于破坏性硬件诊断。它确保随后的读取来自外部 SDRAM，
  *         而不是 CPU 仍持有的 Cache 副本；不会用于 DMA 缓冲区。
  */
static void platform_sdram_commit_word(const volatile uint16_t *address)
{
    const uintptr_t cache_line_address =
        ((uintptr_t)address) & ~((uintptr_t)CORTEX_M7_DCACHE_LINE_SIZE - 1U);

    (void)CortexM7DCache_CleanInvalidateRange(
        (const void *)cache_line_address,
        CORTEX_M7_DCACHE_LINE_SIZE);
}

/**
  * @brief  对整块 SDRAM 执行 D-Cache Clean，确保 CPU 写入真正到达外部存储器。
 * @note   SDRAM 基地址和容量均为 32 字节整数倍，满足 Cortex-M7 Cache Adapter
 *         按完整 Cache line 维护范围的约束。
  */
static void platform_sdram_clean_all(void)
{
    (void)CortexM7DCache_CleanRange(
        (const void *)PLATFORM_SDRAM_BASE_ADDRESS,
        PLATFORM_SDRAM_CAPACITY_BYTES);
}

/**
  * @brief  失效整块 SDRAM 的 D-Cache，使接下来的读取必须从外部 SDRAM 取数。
  */
static void platform_sdram_invalidate_all(void)
{
    (void)CortexM7DCache_InvalidateRange(
        (const void *)PLATFORM_SDRAM_BASE_ADDRESS,
        PLATFORM_SDRAM_CAPACITY_BYTES);
}

/**
  * @brief  保存首次错误的位置和值。
  * @param  diagnostics 当前诊断结果对象。
  * @param  stage 当前失败的测试阶段。
  * @param  address 出现失配的 SDRAM 字地址。
  * @param  expected 应读到的 16 位数据。
  * @param  actual 实际读到的 16 位数据。
  */
static void platform_sdram_set_failure(
    Platform_SDRAM_DiagnosticsTypeDef *diagnostics,
    Platform_SDRAM_DiagnosticStageTypeDef stage,
    const volatile uint16_t *address,
    uint16_t expected,
    uint16_t actual)
{
    diagnostics->FailedStage = stage;
    diagnostics->FailureAddress = (uint32_t)(uintptr_t)address;
    diagnostics->ExpectedValue = expected;
    diagnostics->ActualValue = actual;
}

/**
  * @brief  验证 16 位数据总线的每一位均可被独立写入和读取。
  * @param  diagnostics 当前诊断结果对象。
  * @retval true 数据总线测试通过。
  * @retval false 某一位在写入单 1 或单 0 后读取失配。
  */
static bool platform_sdram_test_data_bus(Platform_SDRAM_DiagnosticsTypeDef *diagnostics)
{
    volatile uint16_t *const memory = (volatile uint16_t *)PLATFORM_SDRAM_BASE_ADDRESS;

    for (uint32_t bit = 0U; bit < 16U; ++bit)
    {
        uint16_t expected = (uint16_t)(PLATFORM_SDRAM_DATA_BUS_PATTERN << bit);

        memory[0] = expected;
        platform_sdram_commit_word(&memory[0]);

        const uint16_t actual = memory[0];
        if (actual != expected)
        {
            platform_sdram_set_failure(diagnostics,
                                       PLATFORM_SDRAM_DIAGNOSTIC_STAGE_DATA_BUS,
                                       &memory[0],
                                       expected,
                                       actual);
            return false;
        }

        expected = (uint16_t)~expected;
        memory[0] = expected;
        platform_sdram_commit_word(&memory[0]);

        const uint16_t inverted_actual = memory[0];
        if (inverted_actual != expected)
        {
            platform_sdram_set_failure(diagnostics,
                                       PLATFORM_SDRAM_DIAGNOSTIC_STAGE_DATA_BUS,
                                       &memory[0],
                                       expected,
                                       inverted_actual);
            return false;
        }
    }

    return true;
}

/**
  * @brief  验证能够影响 SDRAM 寻址范围的每个地址位均不存在镜像或短接。
  * @param  diagnostics 当前诊断结果对象。
  * @retval true 地址总线测试通过。
  * @retval false 基地址或某个二进制地址偏移出现意外联动。
  */
static bool platform_sdram_test_address_bus(Platform_SDRAM_DiagnosticsTypeDef *diagnostics)
{
    volatile uint16_t *const memory = (volatile uint16_t *)PLATFORM_SDRAM_BASE_ADDRESS;
    const uint32_t word_count =
        PLATFORM_SDRAM_CAPACITY_BYTES / PLATFORM_SDRAM_DATA_WIDTH_BYTES;
    const uint16_t pattern = PLATFORM_SDRAM_ADDRESS_PATTERN;
    const uint16_t inverse_pattern = (uint16_t)~pattern;

    memory[0] = pattern;
    platform_sdram_commit_word(&memory[0]);

    /* 依次覆盖每一个地址位代表的最小偏移。 */
    for (uint32_t offset = 1U; offset < word_count; offset <<= 1U)
    {
        memory[offset] = pattern;
        platform_sdram_commit_word(&memory[offset]);
    }

    memory[0] = inverse_pattern;
    platform_sdram_commit_word(&memory[0]);

    for (uint32_t offset = 1U; offset < word_count; offset <<= 1U)
    {
        const uint16_t actual = memory[offset];
        if (actual != pattern)
        {
            platform_sdram_set_failure(diagnostics,
                                       PLATFORM_SDRAM_DIAGNOSTIC_STAGE_ADDRESS_BUS,
                                       &memory[offset],
                                       pattern,
                                       actual);
            return false;
        }
    }

    memory[0] = pattern;
    platform_sdram_commit_word(&memory[0]);

    /* 逐一翻转每个地址位，再确认它没有改写基地址。 */
    for (uint32_t offset = 1U; offset < word_count; offset <<= 1U)
    {
        memory[offset] = inverse_pattern;
        platform_sdram_commit_word(&memory[offset]);

        const uint16_t actual = memory[0];
        if (actual != pattern)
        {
            platform_sdram_set_failure(diagnostics,
                                       PLATFORM_SDRAM_DIAGNOSTIC_STAGE_ADDRESS_BUS,
                                       &memory[0],
                                       pattern,
                                       actual);
            return false;
        }

        memory[offset] = pattern;
        platform_sdram_commit_word(&memory[offset]);
    }

    return true;
}

/**
  * @brief  把 Cortex-M7 周期数换算为毫秒。
  * @param  cycles 已采样的 Cortex-M7 核心周期数。
  * @retval 向下取整后的毫秒数；时钟信息不可用时返回 0。
  */
static uint32_t platform_sdram_cycles_to_milliseconds(uint32_t cycles)
{
    const uint32_t core_frequency_hz = CortexM7CycleCounter_GetFrequencyHz();

    if (core_frequency_hz == 0U)
    {
        return 0U;
    }

    return (uint32_t)(((uint64_t)cycles * 1000ULL) / core_frequency_hz);
}

/**
  * @brief  把指定字节数和 DWT 周期数换算为百分之一 MiB/s。
  * @param  byte_count 本次已访问的数据量。
  * @param  cycles 对应的核心周期数。
  * @retval 吞吐率乘以 100；周期数或时钟信息不可用时返回 0。
  */
static uint32_t platform_sdram_cycles_to_mib_per_second_x100(uint32_t byte_count,
                                                               uint32_t cycles)
{
    const uint32_t core_frequency_hz = CortexM7CycleCounter_GetFrequencyHz();

    if ((cycles == 0U) || (core_frequency_hz == 0U))
    {
        return 0U;
    }

    return (uint32_t)(((uint64_t)byte_count * core_frequency_hz * 100ULL) /
                       ((uint64_t)cycles * 1024ULL * 1024ULL));
}

/**
  * @brief  对整片 SDRAM 写入地址相关图样、提交到外部存储器并逐字校验。
  * @param  diagnostics 当前诊断结果对象。
  * @retval true 全容量图样校验通过。
  * @retval false 存在至少一个读回失配；结果记录首个失配位置。
  * @note   写入耗时包含 D-Cache Clean，因此表示数据真正到达 SDRAM 的 CPU 写入
  *         吞吐；读取前会失效整片 D-Cache，避免把 Cache 命中误报为 SDRAM 速度。
  */
static bool platform_sdram_test_full_pattern(Platform_SDRAM_DiagnosticsTypeDef *diagnostics)
{
    volatile uint16_t *const memory = (volatile uint16_t *)PLATFORM_SDRAM_BASE_ADDRESS;
    const uint32_t word_count =
        PLATFORM_SDRAM_CAPACITY_BYTES / PLATFORM_SDRAM_DATA_WIDTH_BYTES;
    uint32_t cycles;

    (void)CortexM7CycleCounter_Start();
    const uint32_t write_start = CortexM7CycleCounter_Read();

    for (uint32_t index = 0U; index < word_count; ++index)
    {
        memory[index] = (uint16_t)(index ^ PLATFORM_SDRAM_MEMORY_PATTERN_SEED);
    }

    platform_sdram_clean_all();
    cycles = CortexM7CycleCounter_Read() - write_start;
    diagnostics->WriteElapsedMilliseconds = platform_sdram_cycles_to_milliseconds(cycles);
    diagnostics->WriteSpeedMiBPerSecondX100 =
        platform_sdram_cycles_to_mib_per_second_x100(PLATFORM_SDRAM_CAPACITY_BYTES, cycles);

    platform_sdram_invalidate_all();
    (void)CortexM7CycleCounter_Start();
    const uint32_t read_start = CortexM7CycleCounter_Read();

    for (uint32_t index = 0U; index < word_count; ++index)
    {
        const uint16_t expected =
            (uint16_t)(index ^ PLATFORM_SDRAM_MEMORY_PATTERN_SEED);
        const uint16_t actual = memory[index];

        if (actual != expected)
        {
            platform_sdram_set_failure(diagnostics,
                                       PLATFORM_SDRAM_DIAGNOSTIC_STAGE_FULL_PATTERN,
                                       &memory[index],
                                       expected,
                                       actual);
            return false;
        }
    }

    cycles = CortexM7CycleCounter_Read() - read_start;
    diagnostics->ReadElapsedMilliseconds = platform_sdram_cycles_to_milliseconds(cycles);
    diagnostics->ReadSpeedMiBPerSecondX100 =
        platform_sdram_cycles_to_mib_per_second_x100(PLATFORM_SDRAM_CAPACITY_BYTES, cycles);

    return true;
}

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  执行兼容 MT48LC16M16A2-6A 与 AS4C16M16SA-7TCN 的 JEDEC 上电初始化序列。
  * @retval PLATFORM_SDRAM_OK 初始化完成或此前已经完成。
  * @retval PLATFORM_SDRAM_HAL_ERROR FMC 命令或刷新率配置失败。
  * @note   两种器件均为 4 Meg × 16 × 4 Bank、8192 行/64 ms 的 SDR SDRAM；当前
  *         135 MHz、CAS 3、Burst 4 的共用时序已按较慢的 AS4 -7 等级留出裕量。
  *         CubeMX 的 MX_FMC_Init() 只配置 FMC 控制器和引脚；本函数负责使 SDRAM
  *         离开上电未知状态。它在 Platform_Init() 中、任务创建前调用。
  */
Platform_SDRAM_StatusTypeDef Platform_SDRAM_Init(void)
{
    if (platform_sdram_initialized)
    {
        return PLATFORM_SDRAM_OK;
    }

    if (!platform_sdram_send_command(FMC_SDRAM_CMD_CLK_ENABLE, 1U, 0U))
    {
        return PLATFORM_SDRAM_HAL_ERROR;
    }

    /* 规格书要求至少 100 us；此处以 1 ms 满足并保留启动裕量。 */
    HAL_Delay(PLATFORM_SDRAM_POWER_UP_DELAY_MS);

    if (!platform_sdram_send_command(FMC_SDRAM_CMD_PALL, 1U, 0U) ||
        !platform_sdram_send_command(FMC_SDRAM_CMD_AUTOREFRESH_MODE,
                                     PLATFORM_SDRAM_INITIAL_AUTO_REFRESH_COUNT,
                                     0U) ||
        !platform_sdram_send_command(FMC_SDRAM_CMD_LOAD_MODE,
                                     1U,
                                     PLATFORM_SDRAM_MODE_REGISTER_VALUE) ||
        (HAL_SDRAM_ProgramRefreshRate(&hsdram1, PLATFORM_SDRAM_REFRESH_RATE) != HAL_OK))
    {
        return PLATFORM_SDRAM_HAL_ERROR;
    }

    platform_sdram_initialized = true;
    return PLATFORM_SDRAM_OK;
}

/**
  * @brief  执行当前板载 SDRAM 的破坏性数据完整性和吞吐诊断。
  * @param  diagnostics 调用者提供的结果对象，不能为空。
  * @retval PLATFORM_SDRAM_OK 所有测试通过，测速字段有效。
  * @retval PLATFORM_SDRAM_NOT_READY 尚未完成 JEDEC 初始化。
  * @retval PLATFORM_SDRAM_DIAGNOSTIC_FAILED 数据线、地址线、全容量图样或 DWT
  *         周期计数器测试失败；详情写入 diagnostics。
  * @warning 本函数会覆盖完整 32 MiB SDRAM，只能在尚未放置帧缓冲、堆或业务数据
  *          的启动诊断窗口调用。
  */
Platform_SDRAM_StatusTypeDef Platform_SDRAM_RunDiagnostic(
    Platform_SDRAM_DiagnosticsTypeDef *diagnostics)
{
    if (diagnostics == NULL)
    {
        return PLATFORM_SDRAM_DIAGNOSTIC_FAILED;
    }

    (void)memset(diagnostics, 0, sizeof(*diagnostics));
    diagnostics->CapacityBytes = PLATFORM_SDRAM_CAPACITY_BYTES;

    if (!platform_sdram_initialized)
    {
        return PLATFORM_SDRAM_NOT_READY;
    }

    if (!CortexM7CycleCounter_Start())
    {
        diagnostics->FailedStage = PLATFORM_SDRAM_DIAGNOSTIC_STAGE_CYCLE_COUNTER;
        return PLATFORM_SDRAM_DIAGNOSTIC_FAILED;
    }

    if (!platform_sdram_test_data_bus(diagnostics) ||
        !platform_sdram_test_address_bus(diagnostics) ||
        !platform_sdram_test_full_pattern(diagnostics))
    {
        return PLATFORM_SDRAM_DIAGNOSTIC_FAILED;
    }

    return PLATFORM_SDRAM_OK;
}
