/**
  ******************************************************************************
  * @file    platform_flash.c
  * @brief   当前 PCB W25Q256 与 CubeMX QSPI 的 Platform 装配实现。
  *
 * @details
 *          本 Module 长期持有 W25Qxx Device 和 STM32 HAL QSPI Adapter Context，
 *          并把 CubeMX 管理的 hqspi 和本板预期 JEDEC ID 注入其中。启动时还会
 *          校验 SFDP 签名、确保 QE 已开启，并以一次非破坏性的 Quad I/O 原始读
 *          校验当前硬件的 0xEC 读取通路；同时向当前 APP 基准测试开放受限的原始
 *          数组读取，并为 ADR-0009 保留的首尾 4 KiB 扇区提供受限的破坏性
 *          自检。FTL、逻辑扇区、通用擦写和内存映射均尚未接入。
  ******************************************************************************
  */

#include "Platform/flash/platform_flash.h"
#include "Platform/flash/platform_flash_config.h"

#include <stddef.h>

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Adapters/stm32_hal/w25qxx_qspi/w25qxx_qspi_stm32_hal_adapter.h"
#include "Components/w25qxx/w25qxx.h"
#include "quadspi.h"

/** @brief 当前 PCB W25Q256 对应的最小 JEDEC 兼容性要求。 */
static const W25Qxx_ExpectedJedecIDTypeDef platform_flash_expected_jedec_id = {
    .ManufacturerID = PLATFORM_FLASH_EXPECTED_MANUFACTURER_ID,
    .CapacityID = PLATFORM_FLASH_EXPECTED_CAPACITY_ID
};

/** @brief 当前 PCB 唯一 W25Q256 对应的 W25Qxx Device 实例。 */
static W25Qxx_HandleTypeDef hplatform_flash = {
    .ExpectedJedecID = &platform_flash_expected_jedec_id
};

/**
 * @brief 当前 PCB QSPI 外设的 STM32 HAL Adapter Context。
 * @note  hqspi 的创建、GPIO、时钟和 NVIC 均由 CubeMX 管理；Platform 只借用
 *        它并决定它服务于哪一个板级 Device 实例。
 */
static W25Qxx_QSPI_STM32HALAdapterTypeDef hplatform_flash_adapter = {
    .Handle = &hqspi,
    .TimeoutMs = PLATFORM_FLASH_QSPI_TIMEOUT_MS
};

/**
 * @brief  把 Cortex-M7 周期数换算为毫秒。
 * @param  cycles 已采样的核心周期数。
 * @retval 向下取整的毫秒数；核心时钟不可用时返回零。
 */
static uint32_t platform_flash_cycles_to_milliseconds(uint32_t cycles)
{
    const uint32_t core_frequency_hz = CortexM7CycleCounter_GetFrequencyHz();

    if (core_frequency_hz == 0u)
    {
        return 0u;
    }

    return (uint32_t)(((uint64_t)cycles * 1000ULL) / core_frequency_hz);
}

/**
 * @brief  把数据量和 Cortex-M7 周期数换算为百分之一 MiB/s。
 * @param  byte_count 本次完成读写的数据字节数。
 * @param  cycles 对应的端到端核心周期数。
 * @retval 吞吐率乘以 100；周期数或核心时钟不可用时返回零。
 */
static uint32_t platform_flash_cycles_to_mib_per_second_x100(
    uint32_t byte_count,
    uint32_t cycles)
{
    const uint32_t core_frequency_hz = CortexM7CycleCounter_GetFrequencyHz();

    if ((cycles == 0u) || (core_frequency_hz == 0u))
    {
        return 0u;
    }

    return (uint32_t)(((uint64_t)byte_count * core_frequency_hz * 100ULL) /
                      ((uint64_t)cycles * 1024ULL * 1024ULL));
}

/**
 * @brief  生成自检扇区中指定物理地址应保存的地址相关字节图样。
 * @param  address 当前字节的 W25Q256 物理地址。
 * @param  seed 当前自检扇区专属的图样种子。
 * @retval 对应地址的确定性预期字节。
 * @note   两个扇区使用不同种子，既覆盖完整 4 KiB，也不会让低、高地址相同低位
 *         的位置出现相同的测试数据。
 */
static uint8_t platform_flash_diagnostic_pattern_byte(uint32_t address,
                                                       uint32_t seed)
{
    uint32_t value = address ^ seed;

    value ^= value >> 16u;
    value *= 0x7FEB352Du;
    value ^= value >> 15u;
    value *= 0x846CA68Bu;
    value ^= value >> 16u;
    return (uint8_t)value;
}

/**
 * @brief  等待当前由 W25Qxx Component 管理的异步原始操作完成。
 * @retval true 已观察到 WIP 清零且 Component 回到 READY。
 * @retval false 状态读取、总线事务或当前操作超时失败。
 * @note   此同步包装只服务于启动期显式诊断；正常 FTL/MSC 路径必须自行按任务节拍
 *         调用 W25Qxx_Process()，不得在此忙等。
 */
static bool platform_flash_wait_for_diagnostic_operation(void)
{
    W25Qxx_StatusTypeDef status;

    do
    {
        status = W25Qxx_Process(&hplatform_flash);
    } while (status == W25QXX_BUSY);

    return status == W25QXX_OK;
}

/**
 * @brief  填满一个 4 KiB 自检页编程缓冲区。
 * @param  buffer 已验证容量足够的诊断缓冲区。
 * @param  sector_address 当前 4 KiB 自检扇区首地址。
 * @param  seed 当前扇区的专属图样种子。
 */
static void platform_flash_fill_diagnostic_buffer(uint8_t *buffer,
                                                  uint32_t sector_address,
                                                  uint32_t seed)
{
    for (uint32_t offset = 0u;
         offset < PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES;
         ++offset)
    {
        buffer[offset] = platform_flash_diagnostic_pattern_byte(
            sector_address + offset,
            seed);
    }
}

/**
 * @brief  把一个已填充的 4 KiB 诊断缓冲区按 256-byte 页写入指定自检扇区。
 * @param  sector_address 已擦除的 4 KiB 自检扇区首地址。
 * @param  buffer 已填充的 4 KiB 诊断缓冲区。
 * @param  failure_address 接收首个失败页首地址的有效地址。
 * @retval true 所有页已完成非易失化。
 * @retval false 页编程启动或轮询失败。
 */
static bool platform_flash_program_diagnostic_sector(uint32_t sector_address,
                                                      const uint8_t *buffer,
                                                      uint32_t *failure_address)
{
    for (uint32_t offset = 0u;
         offset < PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES;
         offset += W25QXX_PAGE_PROGRAM_MAX_SIZE_BYTES)
    {
        const uint32_t page_address = sector_address + offset;

        if ((W25Qxx_ProgramPageStart(&hplatform_flash,
                                     page_address,
                                     &buffer[offset],
                                     W25QXX_PAGE_PROGRAM_MAX_SIZE_BYTES) != W25QXX_OK) ||
            !platform_flash_wait_for_diagnostic_operation())
        {
            *failure_address = page_address;
            return false;
        }
    }

    return true;
}

/**
 * @brief  读取并校验一个已写入的 4 KiB 自检扇区。
 * @param  sector_address 当前自检扇区首地址。
 * @param  seed 当前扇区的专属图样种子。
 * @param  buffer 接收 4 KiB 读回数据的诊断缓冲区。
 * @param  diagnostics 当前诊断结果；失配时写入地址、预期值和实际值。
 * @retval true 已读回且所有字节均与地址相关图样一致。
 * @retval false 读取失败或存在至少一个失配。
 */
static bool platform_flash_verify_diagnostic_sector(
    uint32_t sector_address,
    uint32_t seed,
    uint8_t *buffer,
    Platform_Flash_DiagnosticsTypeDef *diagnostics)
{
    if (W25Qxx_Read(&hplatform_flash,
                    sector_address,
                    buffer,
                    PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES) != W25QXX_OK)
    {
        diagnostics->FailureAddress = sector_address;
        return false;
    }

    for (uint32_t offset = 0u;
         offset < PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES;
         ++offset)
    {
        const uint8_t expected = platform_flash_diagnostic_pattern_byte(
            sector_address + offset,
            seed);

        if (buffer[offset] != expected)
        {
            diagnostics->FailureAddress = sector_address + offset;
            diagnostics->ExpectedValue = expected;
            diagnostics->ActualValue = buffer[offset];
            return false;
        }
    }

    return true;
}

/**
 * @brief  绑定当前 PCB QSPI Adapter 并配置 W25Q256 的最小可用读取能力。
 * @retval PLATFORM_OK W25Qxx Device 已进入 READY，JEDEC ID、SFDP、QE 与 0xEC
 *         Quad I/O 读取通路均已校验。
 * @retval PLATFORM_FLASH_ERROR Adapter 绑定或芯片识别失败。
 * @note   本函数仅用于启动阶段的一次同步硬件识别，必须在 CubeMX 已完成
 *         MX_QUADSPI_Init() 后调用。QE=0 时会执行一次最多 20 ms 的 SR2 写入
 *         轮询；当前不注册 QSPI 完成回调，也不使用已启用的 QUADSPI IRQ。扇区
 *         自检以外的异步页编程、自动状态轮询与 FTL 状态机仍待后续定义。末尾会
 *         从物理地址 0 读取 4 字节，但不解释其内容、不修改 Flash；该地址满足
 *         0xEC 的 4-byte 起始地址对齐要求。
 */
Platform_StatusTypeDef Platform_Flash_Init(void)
{
    uint8_t quad_read_probe[W25QXX_QUAD_READ_ADDRESS_ALIGNMENT_BYTES];

    if (W25Qxx_QSPI_STM32HALAdapter_Bind(
            &hplatform_flash,
            &hplatform_flash_adapter) != W25QXX_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }

    if (W25Qxx_Init(&hplatform_flash) != W25QXX_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }

    if (W25Qxx_ProbeSFDP(&hplatform_flash) != W25QXX_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }

    if (W25Qxx_EnsureQuadEnabled(&hplatform_flash) != W25QXX_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }

    return (W25Qxx_Read(&hplatform_flash,
                         0u,
                         quad_read_probe,
                         sizeof(quad_read_probe)) == W25QXX_OK) ?
               PLATFORM_OK :
               PLATFORM_FLASH_ERROR;
}

/**
 * @brief  获取当前 PCB W25Q256 在初始化阶段缓存的 JEDEC ID。
 * @param  jedec_id 接收三字节芯片标识的有效地址。
 * @retval PLATFORM_OK 已返回缓存的 JEDEC ID。
 * @retval PLATFORM_FLASH_ERROR 参数无效，或 Flash 尚未成功初始化。
 * @note   本函数不会重新访问 QSPI；它只把 W25Qxx Device 的可移植标识类型转换
 *         成 Platform 对上的板级表达，避免上层包含 Component 头文件。
 */
Platform_StatusTypeDef Platform_Flash_GetJedecID(
    Platform_Flash_JedecIDTypeDef *jedec_id)
{
    W25Qxx_JedecIDTypeDef device_id;

    if (jedec_id == NULL)
    {
        return PLATFORM_FLASH_ERROR;
    }

    if (W25Qxx_GetJedecID(&hplatform_flash, &device_id) != W25QXX_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }

    jedec_id->ManufacturerID = device_id.ManufacturerID;
    jedec_id->MemoryType = device_id.MemoryType;
    jedec_id->CapacityID = device_id.CapacityID;
    return PLATFORM_OK;
}

/**
 * @brief  读取当前 PCB W25Q256 的实时状态寄存器。
 * @param  status_registers 接收 SR1、SR2 及 WIP、WEL、QE 语义的有效地址。
 * @retval PLATFORM_OK 已完成读取并复制当前状态。
 * @retval PLATFORM_FLASH_ERROR 参数无效或 W25Qxx Device 未成功读取状态。
 * @note   本函数不返回启动缓存，每次调用均访问 QSPI；WIP/WEL 是瞬态状态，调用者
 *         不应把本次快照用于未来事务的并发判定。
 */
Platform_StatusTypeDef Platform_Flash_ReadStatusRegisters(
    Platform_Flash_StatusRegistersTypeDef *status_registers)
{
    W25Qxx_StatusRegistersTypeDef device_status_registers;

    if (status_registers == NULL)
    {
        return PLATFORM_FLASH_ERROR;
    }

    if (W25Qxx_ReadStatusRegisters(&hplatform_flash,
                                   &device_status_registers) != W25QXX_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }

    status_registers->StatusRegister1 = device_status_registers.StatusRegister1;
    status_registers->StatusRegister2 = device_status_registers.StatusRegister2;
    status_registers->IsWriteInProgress =
        device_status_registers.IsWriteInProgress;
    status_registers->IsWriteEnabled = device_status_registers.IsWriteEnabled;
    status_registers->IsQuadEnabled = device_status_registers.IsQuadEnabled;
    return PLATFORM_OK;
}

/**
 * @brief  以当前 W25Q256 的 0xEC Quad I/O 事务读取物理数组数据。
 * @param  address 首个待读字节的物理 Flash 地址，必须 4-byte 对齐。
 * @param  data 接收读取数据的有效缓冲区。
 * @param  data_length 待读取的非零字节数，且不得超出芯片物理容量。
 * @retval PLATFORM_OK 数据已同步写入 data。
 * @retval PLATFORM_FLASH_ERROR Flash 未就绪，或地址、对齐、长度或底层 QSPI
 *         事务无效。
 * @note   此 Interface 仅用于当前 FTL 接入前的启动验证和 APP 读取基准。它保留
 *         W25Q256 0xEC 的首地址 4-byte 对齐约束，不扩展为逻辑地址接口，也不向
 *         上层泄漏 W25Qxx Handle 或 HAL QSPI Handle。
 */
Platform_StatusTypeDef Platform_Flash_ReadArray(
    uint32_t address,
    uint8_t *data,
    uint32_t data_length)
{
    return (W25Qxx_Read(&hplatform_flash, address, data, data_length) == W25QXX_OK) ?
               PLATFORM_OK :
               PLATFORM_FLASH_ERROR;
}

/**
 * @brief  破坏性验证 ADR-0009 保留的首、尾 4 KiB W25Q256 自检扇区并统计时序。
 * @param  buffer 调用者提供的 4 KiB 临时缓冲区；必须可由 CPU 访问且容量不少于
 *         PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES。
 * @param  buffer_size buffer 的实际容量，单位为字节。
 * @param  diagnostics 接收测试范围、首个错误和 DWT 计时结果的有效地址。
 * @retval PLATFORM_OK 首尾扇区均完成擦除、16 页编程、读回和逐字节校验。
 * @retval PLATFORM_FLASH_ERROR 参数无效、DWT 不可用，或任一底层 Flash 阶段失败。
 * @warning 本函数会反复擦除并覆写固定的首、尾 4 KiB 扇区；它们由 ADR-0009 永久
 *          保留，不得保存 FTL、资源包、镜像槽或用户数据。仅可在显式诊断宏开启、
 *          Storage Task 启动早期且 Flash 无其他使用者时调用。
 * @note   擦除和编程各保持 W25Qxx Component 的异步 Start/Process 语义；本函数仅
 *         因为属于显式板测而同步等待。DWT 时序包含命令提交、WIP 轮询和同步 QSPI
 *         传输，读速和写速不是理论总线带宽。
 */
Platform_StatusTypeDef Platform_Flash_RunDiagnostic(
    uint8_t *buffer,
    uint32_t buffer_size,
    Platform_Flash_DiagnosticsTypeDef *diagnostics)
{
    uint32_t start_cycles;
    uint32_t elapsed_cycles;
    uint32_t program_cycles;

    if ((buffer == NULL) ||
        (buffer_size < PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES) ||
        (diagnostics == NULL))
    {
        return PLATFORM_FLASH_ERROR;
    }

    *diagnostics = (Platform_Flash_DiagnosticsTypeDef){0};
    diagnostics->TestedBytes = 2u * PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES;

    if (!CortexM7CycleCounter_Start())
    {
        diagnostics->FailedStage = PLATFORM_FLASH_DIAGNOSTIC_STAGE_CYCLE_COUNTER;
        return PLATFORM_FLASH_ERROR;
    }

    start_cycles = CortexM7CycleCounter_Read();
    if ((W25Qxx_SectorEraseStart(&hplatform_flash,
                                 PLATFORM_FLASH_DIAGNOSTIC_HEAD_SECTOR_ADDRESS) != W25QXX_OK) ||
        !platform_flash_wait_for_diagnostic_operation())
    {
        diagnostics->FailedStage = PLATFORM_FLASH_DIAGNOSTIC_STAGE_HEAD_ERASE;
        diagnostics->FailureAddress = PLATFORM_FLASH_DIAGNOSTIC_HEAD_SECTOR_ADDRESS;
        return PLATFORM_FLASH_ERROR;
    }

    if ((W25Qxx_SectorEraseStart(&hplatform_flash,
                                 PLATFORM_FLASH_DIAGNOSTIC_TAIL_SECTOR_ADDRESS) != W25QXX_OK) ||
        !platform_flash_wait_for_diagnostic_operation())
    {
        diagnostics->FailedStage = PLATFORM_FLASH_DIAGNOSTIC_STAGE_TAIL_ERASE;
        diagnostics->FailureAddress = PLATFORM_FLASH_DIAGNOSTIC_TAIL_SECTOR_ADDRESS;
        return PLATFORM_FLASH_ERROR;
    }

    elapsed_cycles = CortexM7CycleCounter_Read() - start_cycles;
    diagnostics->EraseElapsedMilliseconds =
        platform_flash_cycles_to_milliseconds(elapsed_cycles);

    platform_flash_fill_diagnostic_buffer(
        buffer,
        PLATFORM_FLASH_DIAGNOSTIC_HEAD_SECTOR_ADDRESS,
        PLATFORM_FLASH_DIAGNOSTIC_HEAD_PATTERN_SEED);
    if (!CortexM7CycleCounter_Start())
    {
        diagnostics->FailedStage = PLATFORM_FLASH_DIAGNOSTIC_STAGE_CYCLE_COUNTER;
        return PLATFORM_FLASH_ERROR;
    }

    start_cycles = CortexM7CycleCounter_Read();
    if (!platform_flash_program_diagnostic_sector(
            PLATFORM_FLASH_DIAGNOSTIC_HEAD_SECTOR_ADDRESS,
            buffer,
            &diagnostics->FailureAddress))
    {
        diagnostics->FailedStage = PLATFORM_FLASH_DIAGNOSTIC_STAGE_HEAD_PROGRAM;
        return PLATFORM_FLASH_ERROR;
    }

    program_cycles = CortexM7CycleCounter_Read() - start_cycles;

    platform_flash_fill_diagnostic_buffer(
        buffer,
        PLATFORM_FLASH_DIAGNOSTIC_TAIL_SECTOR_ADDRESS,
        PLATFORM_FLASH_DIAGNOSTIC_TAIL_PATTERN_SEED);
    if (!CortexM7CycleCounter_Start())
    {
        diagnostics->FailedStage = PLATFORM_FLASH_DIAGNOSTIC_STAGE_CYCLE_COUNTER;
        return PLATFORM_FLASH_ERROR;
    }

    start_cycles = CortexM7CycleCounter_Read();
    if (!platform_flash_program_diagnostic_sector(
            PLATFORM_FLASH_DIAGNOSTIC_TAIL_SECTOR_ADDRESS,
            buffer,
            &diagnostics->FailureAddress))
    {
        diagnostics->FailedStage = PLATFORM_FLASH_DIAGNOSTIC_STAGE_TAIL_PROGRAM;
        return PLATFORM_FLASH_ERROR;
    }

    elapsed_cycles = program_cycles + (CortexM7CycleCounter_Read() - start_cycles);
    diagnostics->ProgramElapsedMilliseconds =
        platform_flash_cycles_to_milliseconds(elapsed_cycles);
    diagnostics->ProgramSpeedMiBPerSecondX100 =
        platform_flash_cycles_to_mib_per_second_x100(diagnostics->TestedBytes,
                                                      elapsed_cycles);

    if (!CortexM7CycleCounter_Start())
    {
        diagnostics->FailedStage = PLATFORM_FLASH_DIAGNOSTIC_STAGE_CYCLE_COUNTER;
        return PLATFORM_FLASH_ERROR;
    }

    start_cycles = CortexM7CycleCounter_Read();
    if (!platform_flash_verify_diagnostic_sector(
            PLATFORM_FLASH_DIAGNOSTIC_HEAD_SECTOR_ADDRESS,
            PLATFORM_FLASH_DIAGNOSTIC_HEAD_PATTERN_SEED,
            buffer,
            diagnostics))
    {
        diagnostics->FailedStage =
            (diagnostics->ExpectedValue == diagnostics->ActualValue) ?
                PLATFORM_FLASH_DIAGNOSTIC_STAGE_HEAD_READBACK :
                PLATFORM_FLASH_DIAGNOSTIC_STAGE_HEAD_VERIFY;
        return PLATFORM_FLASH_ERROR;
    }

    if (!platform_flash_verify_diagnostic_sector(
            PLATFORM_FLASH_DIAGNOSTIC_TAIL_SECTOR_ADDRESS,
            PLATFORM_FLASH_DIAGNOSTIC_TAIL_PATTERN_SEED,
            buffer,
            diagnostics))
    {
        diagnostics->FailedStage =
            (diagnostics->ExpectedValue == diagnostics->ActualValue) ?
                PLATFORM_FLASH_DIAGNOSTIC_STAGE_TAIL_READBACK :
                PLATFORM_FLASH_DIAGNOSTIC_STAGE_TAIL_VERIFY;
        return PLATFORM_FLASH_ERROR;
    }

    elapsed_cycles = CortexM7CycleCounter_Read() - start_cycles;
    diagnostics->ReadElapsedMilliseconds =
        platform_flash_cycles_to_milliseconds(elapsed_cycles);
    diagnostics->ReadSpeedMiBPerSecondX100 =
        platform_flash_cycles_to_mib_per_second_x100(diagnostics->TestedBytes,
                                                      elapsed_cycles);
    return PLATFORM_OK;
}
