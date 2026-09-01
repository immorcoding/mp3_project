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
 *          自检。FTL 实例、Bridge、分区与工作内存也由本 Module 长期持有，
 *          向 Service 提供逻辑卷能力；不提供任意物理擦写。H7 QSPI 只读映射
 *          在 WIP 与整个间接请求的互斥约束下统一管理。
 ******************************************************************************
 */

#include "Platform/flash/platform_flash.h"
#include "Platform/flash/platform_flash_config.h"

#include <stddef.h>

#include "Adapters/stm32_hal/irq/stm32_qspi_irq.h"
#include "Adapters/stm32_hal/w25qxx_qspi/w25qxx_qspi_stm32_hal_adapter.h"
#include "Components/w25qxx/w25qxx.h"
#include "quadspi.h"

#include "Components/flash_ftl/flash_ftl.h"
#include "Adapters/bridge/flash_ftl_w25qxx/flash_ftl_w25qxx_bridge.h"

static FlashFTL_HandleTypeDef platform_flash_ftl;
static FlashFTL_W25QxxBridgeTypeDef platform_flash_ftl_bridge;
static bool platform_flash_ftl_bound;
static bool platform_flash_ftl_active;
static bool platform_flash_raw_failed;
static bool platform_flash_needs_wait;
static FlashFTL_StatusTypeDef platform_flash_ftl_result = FLASH_FTL_NOT_READY;
/* 按物理数据块上限分配表，不跨 Module 引用 FTL 私有预留比例。 */
static uint32_t platform_flash_map[PLATFORM_FLASH_FTL_DATA_BLOCKS]
    __attribute__((section(".flash_ftl_tables"), aligned(32)));
static uint64_t platform_flash_versions[PLATFORM_FLASH_FTL_DATA_BLOCKS]
    __attribute__((section(".flash_ftl_tables"), aligned(32)));
static uint8_t platform_flash_states[PLATFORM_FLASH_FTL_DATA_BLOCKS]
    __attribute__((section(".flash_ftl_tables"), aligned(32)));
static uint8_t platform_flash_work[FLASH_FTL_BLOCK_BYTES]
    __attribute__((section(".flash_write_buffer"), aligned(32)));
static uint8_t platform_flash_scratch[FLASH_FTL_SECTOR_BYTES]
    __attribute__((section(".flash_write_buffer"), aligned(32)));

/** @brief 当前 PCB W25Q256 对应的最小 JEDEC 兼容性要求。 */
static const W25Qxx_ExpectedJedecIDTypeDef platform_flash_expected_jedec_id = {
    .ManufacturerID = PLATFORM_FLASH_EXPECTED_MANUFACTURER_ID,
    .CapacityID = PLATFORM_FLASH_EXPECTED_CAPACITY_ID};

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

/** @brief 当前 PCB QSPI 对应的 IRQ Adapter 注册节点。 */
static STM32QSPIIRQ_CallbackTypeDef hplatform_flash_irq_callback;

/** @brief 当前 Platform Flash 的唯一异步 QSPI 操作事件订阅者。 */
static Platform_Flash_OperationCallback_t hplatform_flash_operation_callback;

/** @brief 原样传给异步操作事件订阅者的不透明上下文。 */
static void *hplatform_flash_operation_context;

/** @brief QSPI IRQ Adapter 节点是否已经注册到 hqspi。 */
static bool hplatform_flash_irq_registered;

/** @brief 当前是否存在由 Platform Flash 启动且尚未收尾的异步 QSPI 操作。 */
static volatile bool hplatform_flash_operation_active;

/** @brief 当前 H7 QSPI 是否已处于由本 Module 开启的内存映射模式。 */
static bool hplatform_flash_memory_mapped_mode_enabled;

/** @brief 当前异步间接操作收尾成功后是否应恢复先前开启的内存映射模式。 */
static bool hplatform_flash_restore_memory_mapped_mode_after_operation;

/**
 * @brief 将 W25Qxx 异步 API 返回状态转换为 Platform 通用状态。
 * @param status W25Qxx Device 返回的立即或推进状态。
 * @retval PLATFORM_OK 当前操作已经完成或成功启动。
 * @retval PLATFORM_BUSY 异步操作仍在进行。
 * @retval PLATFORM_FLASH_ERROR 参数、状态机或底层总线失败。
 */
static Platform_StatusTypeDef platform_flash_map_w25qxx_status(
    W25Qxx_StatusTypeDef status)
{
    switch (status)
    {
        case W25QXX_OK:
            return PLATFORM_OK;

        case W25QXX_BUSY:
            return PLATFORM_BUSY;

        case W25QXX_ERROR:
        default:
            return PLATFORM_FLASH_ERROR;
    }
}

/**
 * @brief 把 QSPI IRQ Adapter 的源事件转换为 Platform Flash 操作生命周期事件。
 * @param event QSPI IRQ Adapter 已归类的 HAL 事件。
 * @param context 未使用；保留以符合 STM32QSPIIRQ Handler 签名。
 * @note  本函数运行在 QSPI IRQ 上下文。它先把完成结果写入 QSPI Adapter，再仅在
 *        当前有操作在飞且已订阅时调用上层轻量通知；不推进 W25Qxx 状态机，不做
 *        Cache 维护，不访问 DMA 缓冲区，也不包含 FreeRTOS。
 */
static void platform_flash_qspi_irq_callback(STM32QSPIIRQ_EventTypeDef event,
                                             void *context)
{
    Platform_Flash_OperationEventTypeDef platform_event;

    (void)context;

    switch (event)
    {
        case STM32QSPIIRQ_EVENT_READ_COMPLETE:
            W25Qxx_QSPI_STM32HALAdapter_NotifyReadComplete(
                &hplatform_flash_adapter);
            platform_event = PLATFORM_FLASH_OPERATION_EVENT_READ_COMPLETE;
            break;

        case STM32QSPIIRQ_EVENT_STATUS_MATCH:
            W25Qxx_QSPI_STM32HALAdapter_NotifyStatusMatch(
                &hplatform_flash_adapter);
            platform_event = PLATFORM_FLASH_OPERATION_EVENT_STATUS_MATCH;
            break;

        case STM32QSPIIRQ_EVENT_ABORTED:
            W25Qxx_QSPI_STM32HALAdapter_NotifyOperationError(
                &hplatform_flash_adapter);
            platform_event = PLATFORM_FLASH_OPERATION_EVENT_ABORTED;
            break;

        case STM32QSPIIRQ_EVENT_ERROR:
        default:
            W25Qxx_QSPI_STM32HALAdapter_NotifyOperationError(
                &hplatform_flash_adapter);
            platform_event = PLATFORM_FLASH_OPERATION_EVENT_ERROR;
            break;
    }

    if (hplatform_flash_operation_active &&
        (hplatform_flash_operation_callback != NULL))
    {
        hplatform_flash_operation_callback(platform_event,
                                           hplatform_flash_operation_context);
    }
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
 * @brief  把公开的自检区域语义转换为 Platform 私有的物理地址和图样种子。
 * @param  region 调用者选择的首或尾保留自检扇区。
 * @param  sector_address 可选的有效地址，接收对应 4 KiB 扇区首地址。
 * @param  seed 可选的有效地址，接收对应地址相关图样种子。
 * @retval true region 已识别。
 * @retval false region 无效。
 * @note   只有本 Module 维护 ADR-0009 的物理地址和图样；APP 只能传递区域语义。
 */
static bool platform_flash_get_diagnostic_region(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint32_t *sector_address,
    uint32_t *seed)
{
    uint32_t selected_sector_address;
    uint32_t selected_seed;

    switch (region)
    {
        case PLATFORM_FLASH_DIAGNOSTIC_REGION_HEAD:
            selected_sector_address = PLATFORM_FLASH_DIAGNOSTIC_HEAD_SECTOR_ADDRESS;
            selected_seed = PLATFORM_FLASH_DIAGNOSTIC_HEAD_PATTERN_SEED;
            break;

        case PLATFORM_FLASH_DIAGNOSTIC_REGION_TAIL:
            selected_sector_address = PLATFORM_FLASH_DIAGNOSTIC_TAIL_SECTOR_ADDRESS;
            selected_seed = PLATFORM_FLASH_DIAGNOSTIC_TAIL_PATTERN_SEED;
            break;

        default:
            return false;
    }

    if (sector_address != NULL)
    {
        *sector_address = selected_sector_address;
    }

    if (seed != NULL)
    {
        *seed = selected_seed;
    }

    return true;
}

/**
 * @brief  校验一个已接收的 4 KiB 自检扇区缓冲区。
 * @param  sector_address 当前自检扇区首地址。
 * @param  seed 当前扇区的专属图样种子。
 * @param  buffer 已由同步或 MDMA 读取填满的 4 KiB 缓冲区。
 * @retval true 缓冲区所有字节均与地址相关图样一致。
 * @retval false 发现至少一个字节失配。
 */
static bool platform_flash_compare_diagnostic_buffer(
    uint32_t sector_address,
    uint32_t seed,
    const uint8_t *buffer)
{
    for (uint32_t offset = 0u;
         offset < PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES;
         ++offset)
    {
        const uint8_t expected = platform_flash_diagnostic_pattern_byte(
            sector_address + offset,
            seed);

        if (buffer[offset] != expected)
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief 在 Flash 已空闲时配置 H7 QSPI 的只读内存映射模式。
 * @retval PLATFORM_OK 映射已开启，或此前已保持开启。
 * @retval PLATFORM_BUSY 当前仍有异步操作，或 SR1.WIP 尚未清零。
 * @retval PLATFORM_FLASH_ERROR 初始化、协议描述、状态读取或 HAL 配置失败。
 * @note 仅本 Module 调用此私有函数，以收敛 WIP 与 QSPI 模式互斥约束。调用前
 *       必须不存在映射；若已经映射，直接保持当前模式而不重复发送状态读取。
 */
static Platform_StatusTypeDef platform_flash_enable_memory_mapped_mode_internal(void)
{
    W25Qxx_StatusRegistersTypeDef status_registers;
    W25Qxx_ArrayReadProtocolTypeDef protocol;
    W25Qxx_BusStatusTypeDef bus_status;

    if (!hplatform_flash_irq_registered)
    {
        return PLATFORM_FLASH_ERROR;
    }

    if (hplatform_flash_operation_active)
    {
        return PLATFORM_BUSY;
    }

    if (hplatform_flash_memory_mapped_mode_enabled)
    {
        return PLATFORM_OK;
    }

    if (W25Qxx_ReadStatusRegisters(&hplatform_flash, &status_registers) !=
        W25QXX_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }

    if (status_registers.IsWriteInProgress)
    {
        return PLATFORM_BUSY;
    }

    if (W25Qxx_GetArrayReadProtocol(&hplatform_flash, &protocol) != W25QXX_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }

    bus_status = W25Qxx_QSPI_STM32HALAdapter_EnableMemoryMappedMode(
        &hplatform_flash_adapter,
        &protocol);
    if (bus_status == W25QXX_BUS_BUSY)
    {
        return PLATFORM_BUSY;
    }

    if (bus_status != W25QXX_BUS_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }

    hplatform_flash_memory_mapped_mode_enabled = true;
    return PLATFORM_OK;
}

/**
 * @brief 退出当前 H7 QSPI 内存映射模式，使间接事务可以安全提交。
 * @retval PLATFORM_OK 当前未映射，或已成功退出映射。
 * @retval PLATFORM_FLASH_ERROR HAL 未能停止内存映射控制器。
 * @note 此操作不会中止 NOR 内部写擦；它只解除 H7 QSPI 的映射模式。
 */
static Platform_StatusTypeDef platform_flash_disable_memory_mapped_mode_internal(void)
{
    if (!hplatform_flash_memory_mapped_mode_enabled)
    {
        return PLATFORM_OK;
    }

    if (W25Qxx_QSPI_STM32HALAdapter_DisableMemoryMappedMode(
            &hplatform_flash_adapter) != W25QXX_BUS_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }

    hplatform_flash_memory_mapped_mode_enabled = false;
    return PLATFORM_OK;
}

/**
 * @brief 为一次间接 QSPI 操作退出映射，并记录之后是否需要恢复。
 * @param restore_after_access 接收本操作完成后是否恢复原映射状态的有效地址。
 * @retval PLATFORM_OK 已可安全执行间接事务。
 * @retval PLATFORM_FLASH_ERROR 映射退出失败或参数无效。
 * @note 调用者必须只在普通上下文调用；同步操作在结束后立即恢复，异步操作由
 *       Platform_Flash_ProcessOperation() 在成功收尾后恢复。
 */
static Platform_StatusTypeDef platform_flash_prepare_indirect_access(
    bool *restore_after_access)
{
    if (restore_after_access == NULL)
    {
        return PLATFORM_FLASH_ERROR;
    }

    *restore_after_access = hplatform_flash_memory_mapped_mode_enabled;
    if (!*restore_after_access)
    {
        return PLATFORM_OK;
    }

    return platform_flash_disable_memory_mapped_mode_internal();
}

/**
 * @brief 在一次成功结束的间接操作后恢复此前开启的内存映射模式。
 * @param restore_after_access 是否需要恢复本次操作前的映射状态。
 * @retval PLATFORM_OK 无需恢复，或映射已恢复。
 * @retval PLATFORM_FLASH_ERROR 映射恢复失败。
 * @note 对页编程和擦除，调用此函数前的自动状态匹配已确认 WIP 清零；函数仍会
 *       读取一次状态寄存器以统一正常恢复与启动期显式开启的安全条件。
 */
static Platform_StatusTypeDef platform_flash_restore_memory_mapped_mode_if_needed(
    bool restore_after_access)
{
    return restore_after_access ?
               platform_flash_enable_memory_mapped_mode_internal() :
               PLATFORM_OK;
}

/**
 * @brief  绑定当前 PCB QSPI Adapter 并配置 W25Q256 的最小可用读取能力。
 * @retval PLATFORM_OK W25Qxx Device 已进入 READY，JEDEC ID、SFDP、QE 与 0xEC
 *         Quad I/O 读取通路均已校验。
 * @retval PLATFORM_FLASH_ERROR Adapter 绑定或芯片识别失败。
 * @note   本函数仅用于启动阶段的一次同步硬件识别，必须在 CubeMX 已完成
 *         MX_QUADSPI_Init() 后调用。QE=0 时会执行一次最多 20 ms 的 SR2 写入
 *         轮询；同步识别成功后会为 hqspi 注册 MDMA 接收完成、自动状态匹配、错误
 *         和中止回调，供异步诊断与 FTL 请求复用。资源包写入另行设计；
 *         FTL 在 Service 打开卷时独立绑定。末尾从物理地址 0 读取 4 字节，
 *         但不解释其内容、
 *         不修改 Flash；该地址满足 0xEC 的 4-byte 起始地址对齐要求。
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

    hplatform_flash_memory_mapped_mode_enabled = false;
    hplatform_flash_restore_memory_mapped_mode_after_operation = false;

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

    if (W25Qxx_Read(&hplatform_flash,
                     0u,
                     quad_read_probe,
                     sizeof(quad_read_probe)) != W25QXX_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }

    if (STM32QSPIIRQ_Register(&hplatform_flash_irq_callback,
                              &hqspi,
                              platform_flash_qspi_irq_callback,
                              NULL) != STM32QSPIIRQ_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }

    hplatform_flash_irq_registered = true;
    return PLATFORM_OK;
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
    Platform_StatusTypeDef status;
    bool restore_memory_mapped_mode;

    if (status_registers == NULL)
    {
        return PLATFORM_FLASH_ERROR;
    }

    status = platform_flash_prepare_indirect_access(&restore_memory_mapped_mode);
    if (status != PLATFORM_OK)
    {
        return status;
    }

    if (W25Qxx_ReadStatusRegisters(&hplatform_flash,
                                   &device_status_registers) != W25QXX_OK)
    {
        status = PLATFORM_FLASH_ERROR;
    }
    else
    {
        status_registers->StatusRegister1 = device_status_registers.StatusRegister1;
        status_registers->StatusRegister2 = device_status_registers.StatusRegister2;
        status_registers->IsWriteInProgress =
            device_status_registers.IsWriteInProgress;
        status_registers->IsWriteEnabled = device_status_registers.IsWriteEnabled;
        status_registers->IsQuadEnabled = device_status_registers.IsQuadEnabled;
        status = PLATFORM_OK;
    }

    if ((status == PLATFORM_OK) &&
        (platform_flash_restore_memory_mapped_mode_if_needed(
             restore_memory_mapped_mode) != PLATFORM_OK))
    {
        status = PLATFORM_FLASH_ERROR;
    }

    return status;
}

/**
 * @brief  以当前 W25Q256 的 0xEC Quad I/O 事务读取物理数组数据。
 * @param  address 首个待读字节的物理 Flash 地址，必须 4-byte 对齐。
 * @param  data 接收读取数据的有效缓冲区。
 * @param  data_length 待读取的非零字节数，且不得超出芯片物理容量。
 * @retval PLATFORM_OK 数据已同步写入 data。
 * @retval PLATFORM_FLASH_ERROR Flash 未就绪，或地址、对齐、长度或底层 QSPI
 *         事务无效。
 * @note   此 Interface 仅用于当前 启动诊断使用的的启动验证和 APP 读取基准。它保留
 *         W25Q256 0xEC 的首地址 4-byte 对齐约束，不扩展为逻辑地址接口，也不向
 *         上层泄漏 W25Qxx Handle 或 HAL QSPI Handle。
 */
Platform_StatusTypeDef Platform_Flash_ReadArray(
    uint32_t address,
    uint8_t *data,
    uint32_t data_length)
{
    Platform_StatusTypeDef status;
    bool restore_memory_mapped_mode;

    status = platform_flash_prepare_indirect_access(&restore_memory_mapped_mode);
    if (status != PLATFORM_OK)
    {
        return status;
    }

    status = (W25Qxx_Read(&hplatform_flash, address, data, data_length) == W25QXX_OK) ?
                 PLATFORM_OK :
                 PLATFORM_FLASH_ERROR;
    if ((status == PLATFORM_OK) &&
        (platform_flash_restore_memory_mapped_mode_if_needed(
             restore_memory_mapped_mode) != PLATFORM_OK))
    {
        status = PLATFORM_FLASH_ERROR;
    }

    return status;
}

/**
 * @brief 开启当前 PCB W25Q256 的 H7 QSPI 内存映射模式并返回只读窗口。
 * @param mapped_base 接收映射窗口首地址的有效指针。
 * @param mapped_size 接收当前 W25Q256 可安全读取范围的有效指针。
 * @retval PLATFORM_OK 映射窗口已可读取。
 * @retval PLATFORM_BUSY 仍有异步操作在飞，或 Flash WIP 尚未清零。
 * @retval PLATFORM_FLASH_ERROR 参数无效、Flash 未初始化或无法进入映射模式。
 * @note 返回窗口固定从 H7 的 `0x90000000` 对应 W25Q256 物理 `0x00000000`。
 *       调用者只可读取 `mapped_size` 范围；不得在后续间接写擦期间保留或访问该
 *       指针。Platform 会在间接读写前自动退出映射，并在成功收尾后按先前状态恢复。
 */
Platform_StatusTypeDef Platform_Flash_EnableMemoryMappedMode(
    const uint8_t **mapped_base,
    uint32_t *mapped_size)
{
    Platform_StatusTypeDef status;

    if ((mapped_base == NULL) || (mapped_size == NULL))
    {
        return PLATFORM_FLASH_ERROR;
    }

    status = platform_flash_enable_memory_mapped_mode_internal();
    if (status != PLATFORM_OK)
    {
        return status;
    }

    *mapped_base = (const uint8_t *)(uintptr_t)PLATFORM_FLASH_MEMORY_MAPPED_BASE_ADDRESS;
    *mapped_size = (uint32_t)PLATFORM_FLASH_MEMORY_MAPPED_SIZE_BYTES;
    return PLATFORM_OK;
}

/**
 * @brief  设置本板 W25Q256 唯一的异步 QSPI 操作事件订阅者。
 * @param  operation_callback 在 QSPI IRQ 中调用的有效轻量回调。
 * @param  operation_context 原样传给 operation_callback 的不透明上下文。
 * @retval PLATFORM_OK 订阅者已保存。
 * @retval PLATFORM_FLASH_ERROR Flash 未完成初始化、回调无效或当前操作仍在飞。
 * @note   这是单订阅者 Interface，不是回调链表。回调只能用于唤醒拥有请求的
 *         任务；数据可用性和页写/擦除完成语义均由
 *         Platform_Flash_ProcessOperation() 在普通上下文确认。
 */
Platform_StatusTypeDef Platform_Flash_SetOperationCallback(
    Platform_Flash_OperationCallback_t operation_callback,
    void *operation_context)
{
    if ((!hplatform_flash_irq_registered) ||
        hplatform_flash_operation_active ||
        (operation_callback == NULL))
    {
        return PLATFORM_FLASH_ERROR;
    }

    hplatform_flash_operation_callback = operation_callback;
    hplatform_flash_operation_context = operation_context;
    return PLATFORM_OK;
}

/**
 * @brief  清除本板 W25Q256 的异步 QSPI 操作事件订阅者。
 * @retval PLATFORM_OK 当前订阅者已清除。
 * @retval PLATFORM_FLASH_ERROR Flash 未完成初始化或仍有操作在飞。
 * @note   正常操作完成后调用者可以回收自己的任务上下文。当前 QSPI IRQ Adapter
 *         节点随 Platform Flash 生命周期长期保留，它只在存在操作在飞时才向上发布
 *         事件，因此清除订阅者不影响后续重新订阅。
 */
Platform_StatusTypeDef Platform_Flash_ClearOperationCallback(void)
{
    if ((!hplatform_flash_irq_registered) || hplatform_flash_operation_active)
    {
        return PLATFORM_FLASH_ERROR;
    }

    hplatform_flash_operation_callback = NULL;
    hplatform_flash_operation_context = NULL;
    return PLATFORM_OK;
}

/**
 * @brief  以 0xEC Quad I/O 命令启动一次由 MDMA 搬运的原始数组读取。
 * @param  address 首个待读字节的物理 Flash 地址，必须 4-byte 对齐。
 * @param  data 接收 MDMA 数据的有效、32-byte 对齐缓冲区。
 * @param  data_length 待读取的非零字节数，且必须为完整 Cache line 并位于芯片内。
 * @retval PLATFORM_OK 读取已启动；完成、错误或中止将通过已订阅的 IRQ 回调通知。
 * @retval PLATFORM_BUSY 当前已有读取在飞。
 * @retval PLATFORM_FLASH_ERROR 未初始化、尚未订阅完成回调或 Component 拒绝请求。
 * @note   本函数不等待。调用者收到通知后必须在同一读取拥有者的普通上下文调用
 *         Platform_Flash_ProcessOperation()，后者完成 D-Cache 失效并把 Component
 *         置回 READY。在此之前不得读取或复用 data，也不得发起任何同步 QSPI
 *         事务。
 */
Platform_StatusTypeDef Platform_Flash_StartReadArray(
    uint32_t address,
    uint8_t *data,
    uint32_t data_length)
{
    Platform_StatusTypeDef status;
    bool restore_memory_mapped_mode;

    if ((!hplatform_flash_irq_registered) ||
        (hplatform_flash_operation_callback == NULL))
    {
        return PLATFORM_FLASH_ERROR;
    }

    if (hplatform_flash_operation_active)
    {
        return PLATFORM_BUSY;
    }

    status = platform_flash_prepare_indirect_access(&restore_memory_mapped_mode);
    if (status != PLATFORM_OK)
    {
        return status;
    }

    hplatform_flash_operation_active = true;
    hplatform_flash_restore_memory_mapped_mode_after_operation =
        restore_memory_mapped_mode;
    /* QSPI IRQ 可能在 W25Qxx_StartRead() 返回前到达，先让 ISR 可见在飞状态。 */
    __DMB();
    status = platform_flash_map_w25qxx_status(
        W25Qxx_StartRead(&hplatform_flash, address, data, data_length));
    if (status != PLATFORM_OK)
    {
        hplatform_flash_restore_memory_mapped_mode_after_operation = false;
        if (hplatform_flash.State == W25QXX_STATE_ERROR)
        {
            /* 启动也可能部分成功；由 Process 完成硬件收尾后再向上报告失败。 */
            platform_flash_raw_failed = true;
            return PLATFORM_OK;
        }
        hplatform_flash_operation_active = false;
        __DMB();
        (void)platform_flash_restore_memory_mapped_mode_if_needed(restore_memory_mapped_mode);
    }

    return status;
}

/**
 * @brief 在普通上下文推进当前 FTL 请求或原始诊断，并完成安全收尾。
 * @retval PLATFORM_OK 整个请求成功完成，先前开启的映射模式也已恢复。
 * @retval PLATFORM_BUSY 仍需软件推进、等待硬件或继续故障收尾。
 * @retval PLATFORM_FLASH_ERROR 没有在飞请求，或请求、校验、硬件、映射恢复失败。
 * @note 每次只派发到 FTL 或 W25Qxx 一条推进链，不能重复推进同一底层操作。
 *       BUSY 后通过 OperationNeedsWait 区分等待和软件继续；通知仅为重查提示。
 *       失败须先确认控制器/DMA 不再访问缓冲，保持映射关闭，等待显式恢复。
 *       仅唯一普通执行上下文调用，禁止在 ISR 访问缓冲或推进请求。
 */
Platform_StatusTypeDef Platform_Flash_ProcessOperation(void)
{
    if (!hplatform_flash_operation_active)
    {
        return PLATFORM_FLASH_ERROR;
    }
    Platform_StatusTypeDef status;
    if (platform_flash_ftl_active)
    {
        FlashFTL_StatusTypeDef result = FlashFTL_Process(&platform_flash_ftl);
        platform_flash_ftl_result = result;
        platform_flash_needs_wait = result == FLASH_FTL_WAIT;
        if (result == FLASH_FTL_RUNNING || result == FLASH_FTL_WAIT)
        {
            return PLATFORM_BUSY;
        }
        status = result == FLASH_FTL_OK ? PLATFORM_OK : PLATFORM_FLASH_ERROR;
        platform_flash_ftl_active = false;
    }
    else
    {
        platform_flash_needs_wait = true;
        status = platform_flash_raw_failed
                     ? PLATFORM_FLASH_ERROR
                     : platform_flash_map_w25qxx_status(W25Qxx_Process(&hplatform_flash));
        if (status == PLATFORM_BUSY)
        {
            return status;
        }
        if (status != PLATFORM_OK)
        {
            platform_flash_raw_failed = true;
            if (W25Qxx_Quiesce(&hplatform_flash) != W25QXX_OK)
            {
                return PLATFORM_BUSY;
            }
        }
    }
    bool restore = hplatform_flash_restore_memory_mapped_mode_after_operation;
    hplatform_flash_operation_active = false;
    hplatform_flash_restore_memory_mapped_mode_after_operation = false;
    platform_flash_raw_failed = false;
    platform_flash_needs_wait = false;
    __DMB();
    if (status == PLATFORM_OK)
    {
        if (platform_flash_restore_memory_mapped_mode_if_needed(restore) != PLATFORM_OK)
        {
            /* 数据可能已提交，但板级模式恢复失败仍须关闭卷入口，等待显式恢复。 */
            platform_flash_ftl_result = FLASH_FTL_IO_ERROR;
            status = PLATFORM_FLASH_ERROR;
        }
    }
    return status;
}

/**
 * @brief  对一个已由 Platform 生成图样的保留自检扇区启动 MDMA 读回。
 * @param  region 要读取的首或尾自检扇区。
 * @param  data 接收 MDMA 数据的有效、32-byte 对齐 4 KiB 缓冲区。
 * @param  data_length data 的实际长度，必须恰为一个 4 KiB 自检扇区。
 * @retval PLATFORM_OK 异步读回已启动，完成后会通知当前唯一订阅者。
 * @retval PLATFORM_BUSY 另一个 Platform Flash 读取仍在飞。
 * @retval PLATFORM_FLASH_ERROR 区域、缓冲区、订阅状态或底层读取无效。
 * @warning 只能在同一启动诊断窗口内、且已通过本 Module 的受限擦除与页编程
 *          写入图样后调用；它不擦除、不编程，也不允许指定任意物理地址。
 * @note   调用者仍必须在收到通知后调用 Platform_Flash_ProcessOperation()，随后再
 *         以 Platform_Flash_VerifyDiagnosticReadBuffer() 逐字节校验数据。
 */
Platform_StatusTypeDef Platform_Flash_StartDiagnosticRead(
    Platform_Flash_DiagnosticRegionTypeDef region, uint8_t *data, uint32_t data_length)
{
    uint32_t sector_address;

    if ((data == NULL) ||
        (data_length != PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES) ||
        !platform_flash_get_diagnostic_region(region, &sector_address, NULL))
    {
        return PLATFORM_FLASH_ERROR;
    }

    return Platform_Flash_StartReadArray(sector_address, data, data_length);
}

/**
 * @brief  验证一次 MDMA 自检读回缓冲区是否保留 Platform 生成的完整图样。
 * @param  region 已读回的首或尾自检扇区。
 * @param  data 已完成 ProcessOperation() 的有效 4 KiB 接收缓冲区。
 * @param  data_length data 的实际长度，必须恰为一个 4 KiB 自检扇区。
 * @retval PLATFORM_OK 所有字节均与上一次诊断写入的图样一致。
 * @retval PLATFORM_FLASH_ERROR 区域或缓冲区无效，或发现任一字节失配。
 * @warning 调用前必须已对同一 data 成功执行 Platform_Flash_ProcessOperation()；
 *          本函数只比较 CPU 可见数据，不会启动、等待或收尾任何 QSPI 事务。
 */
Platform_StatusTypeDef Platform_Flash_VerifyDiagnosticReadBuffer(
    Platform_Flash_DiagnosticRegionTypeDef region,
    const uint8_t *data,
    uint32_t data_length)
{
    uint32_t sector_address;
    uint32_t seed;

    if ((data == NULL) ||
        (data_length != PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES) ||
        !platform_flash_get_diagnostic_region(region, &sector_address, &seed))
    {
        return PLATFORM_FLASH_ERROR;
    }

    return platform_flash_compare_diagnostic_buffer(sector_address,
                                                    seed,
                                                    data) ?
               PLATFORM_OK :
               PLATFORM_FLASH_ERROR;
}

/**
 * @brief  同步读取一个 ADR-0009 保留自检扇区。
 * @param  region 首或尾自检扇区语义。
 * @param[out] buffer 接收完整 4 KiB 扇区的有效 CPU 缓冲区。
 * @param[in] buffer_size buffer 的实际长度，必须恰为一个自检扇区。
 * @retval PLATFORM_OK 数据已由轮询 0xEC 读入 buffer。
 * @retval PLATFORM_FLASH_ERROR 区域、参数或同步读取无效。
 * @note   此 Interface 不暴露物理地址，只用于自检的轮询读回路径；不会等待或
 *         干预任何异步 QSPI 操作。
 */
Platform_StatusTypeDef Platform_Flash_ReadDiagnostic(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint8_t *buffer,
    uint32_t buffer_size)
{
    uint32_t sector_address;

    if ((buffer == NULL) ||
        (buffer_size != PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES) ||
        !platform_flash_get_diagnostic_region(region, &sector_address, NULL))
    {
        return PLATFORM_FLASH_ERROR;
    }

    return Platform_Flash_ReadArray(sector_address, buffer, buffer_size);
}

/**
 * @brief  填充一个 ADR-0009 保留自检扇区的 Platform 私有地址相关图样。
 * @param  region 首或尾自检扇区语义。
 * @param  buffer 接收完整 4 KiB 图样的有效 CPU 缓冲区。
 * @param  buffer_size buffer 的实际容量，必须恰为一个自检扇区。
 * @retval PLATFORM_OK 图样已写入 buffer。
 * @retval PLATFORM_FLASH_ERROR 区域或缓冲区无效。
 * @note   APP 只能请求图样，不能取得物理地址或自行选择种子。缓冲区随后可经
 *         StartDiagnosticPageProgram() 逐页写入。
 */
Platform_StatusTypeDef Platform_Flash_FillDiagnosticBuffer(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint8_t *buffer,
    uint32_t buffer_size)
{
    uint32_t sector_address;
    uint32_t seed;

    if ((buffer == NULL) ||
        (buffer_size != PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES) ||
        !platform_flash_get_diagnostic_region(region, &sector_address, &seed))
    {
        return PLATFORM_FLASH_ERROR;
    }

    platform_flash_fill_diagnostic_buffer(buffer, sector_address, seed);
    return PLATFORM_OK;
}

/**
 * @brief  启动一个 ADR-0009 保留自检扇区的异步 4 KiB 擦除。
 * @param  region 首或尾自检扇区语义。
 * @retval PLATFORM_OK 已提交 0x21 且硬件 WIP 自动轮询已启动。
 * @retval PLATFORM_BUSY 已有异步 QSPI 操作在飞。
 * @retval PLATFORM_FLASH_ERROR 区域、订阅或 Component 状态无效。
 * @note   调用者必须等待 STATUS_MATCH，并在普通上下文调用
 *         Platform_Flash_ProcessOperation()。该 Interface 严格限制为两个自检
 *         扇区，不是任意地址擦除能力。
 */
Platform_StatusTypeDef Platform_Flash_StartDiagnosticErase(
    Platform_Flash_DiagnosticRegionTypeDef region)
{
    uint32_t sector_address;
    Platform_StatusTypeDef status;
    bool restore_memory_mapped_mode;

    if (!platform_flash_get_diagnostic_region(region, &sector_address, NULL) ||
        (!hplatform_flash_irq_registered) ||
        (hplatform_flash_operation_callback == NULL))
    {
        return PLATFORM_FLASH_ERROR;
    }

    if (hplatform_flash_operation_active)
    {
        return PLATFORM_BUSY;
    }

    status = platform_flash_prepare_indirect_access(&restore_memory_mapped_mode);
    if (status != PLATFORM_OK)
    {
        return status;
    }

    hplatform_flash_operation_active = true;
    hplatform_flash_restore_memory_mapped_mode_after_operation =
        restore_memory_mapped_mode;
    __DMB();
    status = platform_flash_map_w25qxx_status(
        W25Qxx_SectorEraseStart(&hplatform_flash, sector_address));
    if (status != PLATFORM_OK)
    {
        hplatform_flash_restore_memory_mapped_mode_after_operation = false;
        if (hplatform_flash.State == W25QXX_STATE_ERROR)
        {
            /* 启动也可能部分成功；由 Process 完成硬件收尾后再向上报告失败。 */
            platform_flash_raw_failed = true;
            return PLATFORM_OK;
        }
        hplatform_flash_operation_active = false;
        __DMB();
        (void)platform_flash_restore_memory_mapped_mode_if_needed(restore_memory_mapped_mode);
    }

    return status;
}

/**
 * @brief  启动一个 ADR-0009 保留自检扇区内的异步单页编程。
 * @param  region 首或尾自检扇区语义。
 * @param  page_offset 页首相对扇区首地址的偏移，必须是 256 的整数倍。
 * @param  data 待写入的有效页数据。
 * @param  data_length 写入长度，范围 1..256，且不得跨越此物理页。
 * @retval PLATFORM_OK 已提交 0x34 且硬件 WIP 自动轮询已启动。
 * @retval PLATFORM_BUSY 已有异步 QSPI 操作在飞。
 * @retval PLATFORM_FLASH_ERROR 区域、页范围、订阅或 Component 状态无效。
 * @note   调用者必须等待 STATUS_MATCH，并在普通上下文调用
 *         Platform_Flash_ProcessOperation()。目标页必须已由受限擦除入口擦除；
 *         此 Interface 只服务自检区，未来 Resource Pack 将拥有独立写入入口。
 */
Platform_StatusTypeDef Platform_Flash_StartDiagnosticPageProgram(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint32_t page_offset,
    const uint8_t *data,
    uint32_t data_length)
{
    uint32_t sector_address;
    Platform_StatusTypeDef status;
    bool restore_memory_mapped_mode;

    if ((data == NULL) ||
        (data_length == 0u) ||
        (data_length > W25QXX_PAGE_PROGRAM_MAX_SIZE_BYTES) ||
        ((page_offset % W25QXX_PAGE_PROGRAM_MAX_SIZE_BYTES) != 0u) ||
        (page_offset >= PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES) ||
        (data_length >
         (PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES - page_offset)) ||
        !platform_flash_get_diagnostic_region(region, &sector_address, NULL) ||
        (!hplatform_flash_irq_registered) ||
        (hplatform_flash_operation_callback == NULL))
    {
        return PLATFORM_FLASH_ERROR;
    }

    if (hplatform_flash_operation_active)
    {
        return PLATFORM_BUSY;
    }

    status = platform_flash_prepare_indirect_access(&restore_memory_mapped_mode);
    if (status != PLATFORM_OK)
    {
        return status;
    }

    hplatform_flash_operation_active = true;
    hplatform_flash_restore_memory_mapped_mode_after_operation =
        restore_memory_mapped_mode;
    __DMB();
    status = platform_flash_map_w25qxx_status(
        W25Qxx_ProgramPageStart(&hplatform_flash,
                                 sector_address + page_offset,
                                 data,
                                 data_length));
    if (status != PLATFORM_OK)
    {
        hplatform_flash_restore_memory_mapped_mode_after_operation = false;
        if (hplatform_flash.State == W25QXX_STATE_ERROR)
        {
            /* 启动也可能部分成功；由 Process 完成硬件收尾后再向上报告失败。 */
            platform_flash_raw_failed = true;
            return PLATFORM_OK;
        }
        hplatform_flash_operation_active = false;
        __DMB();
        (void)platform_flash_restore_memory_mapped_mode_if_needed(restore_memory_mapped_mode);
    }

    return status;
}


/**
 * @brief 绑定 FTL/Bridge、物理分区和长期工作内存，不打开或格式化卷。
 * @retval PLATFORM_OK 已成功绑定，或此前已绑定。
 * @retval PLATFORM_FLASH_ERROR 硬件/IRQ 未就绪、已有操作，或分区与内存校验失败。
 * @note 在 SDRAM 和 W25Qxx 就绪后的唯一普通上下文调用；表置于 SDRAM NOLOAD，
 *       Work/Scratch 置于 DMA 可达且 32 B 对齐的内部 SRAM。首次 Open 重建表。
 */
Platform_StatusTypeDef Platform_Flash_BindVolume(void)
{
    if (platform_flash_ftl_bound)
    {
        return PLATFORM_OK;
    }
    if (hplatform_flash_operation_active || !hplatform_flash_irq_registered)
    {
        return PLATFORM_FLASH_ERROR;
    }
    const FlashFTL_MemoryTypeDef memory = {platform_flash_map,
                                           platform_flash_versions,
                                           platform_flash_states,
                                           PLATFORM_FLASH_FTL_DATA_BLOCKS,
                                           PLATFORM_FLASH_FTL_DATA_BLOCKS,
                                           platform_flash_work,
                                           platform_flash_scratch};
    FlashFTL_StatusTypeDef result = FlashFTL_W25QxxBridge_Bind(&platform_flash_ftl,
                                                               &platform_flash_ftl_bridge,
                                                               &hplatform_flash,
                                                               PLATFORM_FLASH_FTL_BASE_ADDRESS,
                                                               PLATFORM_FLASH_FTL_SIZE_BYTES,
                                                               &memory);
    platform_flash_ftl_bound = result == FLASH_FTL_OK;
    return platform_flash_ftl_bound ? PLATFORM_OK : PLATFORM_FLASH_ERROR;
}

enum
{
    PLATFORM_FLASH_VOLUME_OP_OPEN,
    PLATFORM_FLASH_VOLUME_OP_FORMAT,
    PLATFORM_FLASH_VOLUME_OP_READ,
    PLATFORM_FLASH_VOLUME_OP_WRITE,
    PLATFORM_FLASH_VOLUME_OP_SYNC,
    PLATFORM_FLASH_VOLUME_OP_RECLAIM
};

/**
 * @brief 统一受理 FTL 操作，并在整笔请求期间保持间接访问模式。
 * @param[in] operation 本 Module 的 PLATFORM_FLASH_VOLUME_OP_* 请求类别。
 * @param[in] lba 读写起始逻辑扇区，其他操作传零。
 * @param[out] read_buffer 读取输出缓冲，非读操作传 NULL。
 * @param[in] write_buffer 写入输入缓冲，非写操作传 NULL。
 * @param[in] count 读写扇区数，其他操作传零。
 * @return PLATFORM_OK 表示已受理；PLATFORM_BUSY 表示已有请求；PLATFORM_FLASH_ERROR 表示绑定、硬件、映射切换或启动失败。
 * @note 调用前已注册长期事件回调；仅唯一普通上下文使用。成功仅保存入口映射状态，待整笔请求收尾后再按结果恢复，缓冲在此之前保持有效。
 */
static Platform_StatusTypeDef platform_flash_start_volume(uint32_t operation,
                                                          uint32_t lba,
                                                          uint8_t *read_buffer,
                                                          const uint8_t *write_buffer,
                                                          uint32_t count)
{
    if (hplatform_flash_operation_active)
    {
        return PLATFORM_BUSY;
    }
    if (!platform_flash_ftl_bound || !hplatform_flash_operation_callback ||
        hplatform_flash.State != W25QXX_STATE_READY ||
        platform_flash_ftl_result == FLASH_FTL_IO_ERROR)
    {
        return PLATFORM_FLASH_ERROR;
    }
    bool restore;
    if (platform_flash_prepare_indirect_access(&restore) != PLATFORM_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }
    FlashFTL_StatusTypeDef result;
    switch (operation)
    {
        case PLATFORM_FLASH_VOLUME_OP_OPEN:
            result = FlashFTL_OpenStart(&platform_flash_ftl);
            break;
        case PLATFORM_FLASH_VOLUME_OP_FORMAT:
            result = FlashFTL_FormatStart(&platform_flash_ftl);
            break;
        case PLATFORM_FLASH_VOLUME_OP_READ:
            result = FlashFTL_ReadStart(&platform_flash_ftl, lba, read_buffer, count);
            break;
        case PLATFORM_FLASH_VOLUME_OP_WRITE:
            result = FlashFTL_WriteStart(&platform_flash_ftl, lba, write_buffer, count);
            break;
        case PLATFORM_FLASH_VOLUME_OP_SYNC:
            result = FlashFTL_SyncStart(&platform_flash_ftl);
            break;
        default:
            result = FlashFTL_ReclaimStart(&platform_flash_ftl);
            break;
    }
    if (result != FLASH_FTL_OK)
    {
        (void)platform_flash_restore_memory_mapped_mode_if_needed(restore);
        return result == FLASH_FTL_BUSY ? PLATFORM_BUSY : PLATFORM_FLASH_ERROR;
    }
    platform_flash_ftl_result = FLASH_FTL_RUNNING;
    platform_flash_ftl_active = true;
    platform_flash_raw_failed = false;
    platform_flash_needs_wait = false;
    hplatform_flash_operation_active = true;
    hplatform_flash_restore_memory_mapped_mode_after_operation = restore;
    __DMB();
    return PLATFORM_OK;
}

/**
 * @brief 受理只读 FTL 扫描，重建卷映射和物理块状态。
 * @retval PLATFORM_OK 仅表示受理，须持续 ProcessOperation 取得最终结果。
 * @retval PLATFORM_BUSY 已有请求在飞。
 * @retval PLATFORM_FLASH_ERROR 尚未绑定、硬件未就绪或启动失败。
 * @note 仅唯一普通上下文调用；先建立事件订阅。扫描期间映射窗口关闭，
 *       不自动格式化，不兼容/未完成/损坏等细分结果通过 GetVolumeState 查询。
 */
Platform_StatusTypeDef Platform_Flash_OpenVolumeStart(void)
{
    return platform_flash_start_volume(PLATFORM_FLASH_VOLUME_OP_OPEN, 0, NULL, NULL, 0);
}

/**
 * @brief 受理显式破坏性 FTL 格式化，不建立 FAT 文件系统。
 * @retval PLATFORM_OK 已受理，须持续 ProcessOperation 直到结束。
 * @retval PLATFORM_BUSY 已有请求在飞。
 * @retval PLATFORM_FLASH_ERROR 状态或启动条件不满足。
 * @warning 调用前必须注销文件系统并确认绑定分区内数据可丢弃。
 * @note 仅唯一普通上下文调用；不因打开失败自动触发，失败不保证保留旧卷。
 */
Platform_StatusTypeDef Platform_Flash_FormatVolumeStart(void)
{
    return platform_flash_start_volume(PLATFORM_FLASH_VOLUME_OP_FORMAT, 0, NULL, NULL, 0);
}

/**
 * @brief 受理逻辑扇区读取，不向调用者暴露原始页/擦除块。
 * @param lba 起始逻辑扇区号。
 * @param data 至少 count * 512 B 输出，到最终完成或安全收尾前保持有效。
 * @param count 非零扇区数，完整范围不得越逻辑容量。
 * @retval PLATFORM_OK 仅表示受理，数据须等 ProcessOperation 成功后使用。
 * @retval PLATFORM_BUSY 已有请求在飞。
 * @retval PLATFORM_FLASH_ERROR 参数、卷状态、硬件或启动失败。
 * @note 仅唯一普通上下文调用；用户数据由 CPU 复制，DMA 使用 Platform 私有工作区。
 *       失败时输出可能部分更新，不得继续当作完整结果使用。
 */
Platform_StatusTypeDef Platform_Flash_ReadBlocksStart(uint32_t lba, uint8_t *data, uint32_t count)
{
    return platform_flash_start_volume(PLATFORM_FLASH_VOLUME_OP_READ, lba, data, NULL, count);
}

/**
 * @brief 受理逻辑扇区异地提交写入，首版无 RAM 写回早确认。
 * @param lba 起始逻辑扇区号。
 * @param data 至少 count * 512 B 输入，整个请求最终结束前保持有效且不改写。
 * @param count 非零扇区数，整个范围有效；跨组不保证整体原子。
 * @retval PLATFORM_OK 已受理，提交完成由 ProcessOperation 返回。
 * @retval PLATFORM_BUSY 已有请求在飞。
 * @retval PLATFORM_FLASH_ERROR 参数、卷状态、硬件或启动失败。
 * @note 仅唯一普通上下文调用；用户缓冲由 CPU 复制，不直接提交 DMA。
 *       失败可能已有部分组持久化，不能据错误返回断言没有写入。
 */
Platform_StatusTypeDef Platform_Flash_WriteBlocksStart(uint32_t lba,
                                                       const uint8_t *data,
                                                       uint32_t count)
{
    return platform_flash_start_volume(PLATFORM_FLASH_VOLUME_OP_WRITE, lba, NULL, data, count);
}

/**
 * @brief 受理逻辑卷同步确认，不强制清空全部后台 GC。
 * @retval PLATFORM_OK 已受理，仍须调用 ProcessOperation 确认完成。
 * @retval PLATFORM_BUSY 已有请求在飞，不能用此入口代替该请求的推进。
 * @retval PLATFORM_FLASH_ERROR 卷或硬件未就绪、启动失败。
 * @note 仅唯一普通上下文调用；首版此前成功的写请求已完成提交，没有写回缓存。
 */
Platform_StatusTypeDef Platform_Flash_SyncVolumeStart(void)
{
    return platform_flash_start_volume(PLATFORM_FLASH_VOLUME_OP_SYNC, 0, NULL, NULL, 0);
}

/**
 * @brief 受理一次有限 FTL 回收，最多回收一个失效块。
 * @retval PLATFORM_OK 已受理，是否需要实际擦除由 FTL 决定。
 * @retval PLATFORM_BUSY 已有请求在飞。
 * @retval PLATFORM_FLASH_ERROR 卷未就绪或启动失败。
 * @note 仅唯一普通上下文调用，完成由 ProcessOperation 表达；
 *       映射关闭覆盖整个回收请求，NOR 擦除一旦发起不能保证抢占。
 */
Platform_StatusTypeDef Platform_Flash_ReclaimVolumeStart(void)
{
    return platform_flash_start_volume(PLATFORM_FLASH_VOLUME_OP_RECLAIM, 0, NULL, NULL, 0);
}

/**
 * @brief 查询卷持续状态，不访问硬件。
 * @return 就绪、忙、未格式化、不完整、不兼容、损坏或错误等状态。
 * @note 仅在唯一普通执行上下文查询，不作为跨任务锁；
 *       映射恢复失败也会关闭卷入口，不能仅因 RAM 表存在就认为可用。
 */
Platform_Flash_VolumeStateTypeDef Platform_Flash_GetVolumeState(void)
{
    if (!platform_flash_ftl_bound)
    {
        return PLATFORM_FLASH_VOLUME_RESET;
    }
    if (hplatform_flash_operation_active)
    {
        return PLATFORM_FLASH_VOLUME_BUSY;
    }
    if (platform_flash_ftl_result == FLASH_FTL_IO_ERROR)
    {
        return PLATFORM_FLASH_VOLUME_ERROR;
    }
    if (FlashFTL_IsReady(&platform_flash_ftl) && hplatform_flash.State == W25QXX_STATE_READY)
    {
        return PLATFORM_FLASH_VOLUME_READY;
    }
    switch (platform_flash_ftl_result)
    {
        case FLASH_FTL_UNFORMATTED:
            return PLATFORM_FLASH_VOLUME_UNFORMATTED;
        case FLASH_FTL_INCOMPLETE:
            return PLATFORM_FLASH_VOLUME_INCOMPLETE;
        case FLASH_FTL_INCOMPATIBLE:
            return PLATFORM_FLASH_VOLUME_INCOMPATIBLE;
        case FLASH_FTL_CORRUPT:
            return PLATFORM_FLASH_VOLUME_CORRUPT;
        case FLASH_FTL_NOT_READY:
            return PLATFORM_FLASH_VOLUME_RESET;
        default:
            return PLATFORM_FLASH_VOLUME_ERROR;
    }
}

/**
 * @brief 查询已绑定卷的容量，不暴露物理基址或 FTL 私有表。
 * @param info 接收扇区数、扇区字节数和物理分区字节数。
 * @retval PLATFORM_OK 已复制容量，不等同于卷已打开。
 * @retval PLATFORM_FLASH_ERROR 未绑定或输出无效。
 * @note 在唯一普通执行上下文调用，不访问介质。
 */
Platform_StatusTypeDef Platform_Flash_GetVolumeInfo(Platform_Flash_VolumeInfoTypeDef *info)
{
    FlashFTL_InfoTypeDef raw;
    if (!info || !platform_flash_ftl_bound ||
        FlashFTL_GetInfo(&platform_flash_ftl, &raw) != FLASH_FTL_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }
    *info = (Platform_Flash_VolumeInfoTypeDef){
        raw.SectorCount, FLASH_FTL_SECTOR_BYTES, PLATFORM_FLASH_FTL_SIZE_BYTES};
    return PLATFORM_OK;
}

/**
 * @brief 查询逻辑卷和芯片后端诊断快照，不输出日志。
 * @param diagnostics 接收格式/代次、块计数及归一化芯片错误/端口状态。
 * @retval PLATFORM_OK 已复制快照。
 * @retval PLATFORM_FLASH_ERROR 未绑定或输出指针无效。
 * @note 仅唯一普通执行上下文调用。SessionErases 是数据块累计擦除数，
 *       其他块计数为当前表快照；未就绪时有效/失效计数可能为零。
 */
Platform_StatusTypeDef Platform_Flash_GetVolumeDiagnostics(
    Platform_Flash_VolumeDiagnosticsTypeDef *diagnostics)
{
    FlashFTL_DiagnosticsTypeDef raw;
    if (!diagnostics || !platform_flash_ftl_bound ||
        FlashFTL_GetDiagnostics(&platform_flash_ftl, &raw) != FLASH_FTL_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }
    *diagnostics = (Platform_Flash_VolumeDiagnosticsTypeDef){raw.FormatVersion,
                                                             raw.Epoch,
                                                             raw.ValidGroups,
                                                             raw.FreeBlocks,
                                                             raw.StaleBlocks,
                                                             raw.SessionErases,
                                                             hplatform_flash.ErrorCode,
                                                             hplatform_flash.LastBusStatus};
    return PLATFORM_OK;
}

/**
 * @brief 为正在推进请求的执行者区分硬件等待与可继续的软件步骤。
 * @retval true 等待通知或短周期重查，再调用 ProcessOperation。
 * @retval false 软件仍可继续推进，不表示请求已经完成。
 * @note 仅在 ProcessOperation 返回 BUSY 后查询；最终结果由 ProcessOperation 判定。
 */
bool Platform_Flash_OperationNeedsWait(void)
{
    return !platform_flash_ftl_active || platform_flash_needs_wait;
}

/**
 * @brief 请求当前操作进入故障收尾，取消之后的自动映射恢复。
 * @note 仅唯一普通上下文调用；无在飞请求时无操作。
 *       调用后必须继续 ProcessOperation 到最终结束，不能立即复用缓冲。
 *       不保证回滚已提交组，也不取消 NOR 内部已经启动的擦写。
 */
void Platform_Flash_AbortOperation(void)
{
    if (!hplatform_flash_operation_active)
    {
        return;
    }
    hplatform_flash_restore_memory_mapped_mode_after_operation = false;
    if (platform_flash_ftl_active)
    {
        (void)FlashFTL_Abort(&platform_flash_ftl);
    }
    else
    {
        platform_flash_raw_failed = true;
    }
}

/**
 * @brief 显式恢复控制器及器件状态，确认 NOR WIP 清零与 QE 有效。
 * @retval PLATFORM_OK 硬件恢复成功；Service 随后必须重扫 FTL 并处理旧文件对象。
 * @retval PLATFORM_BUSY 仍有请求、硬件未完成安全停止或 NOR 仍忙，稍后重查。
 * @retval PLATFORM_FLASH_ERROR 映射关闭、状态读取或恢复核验失败。
 * @note 仅唯一普通上下文调用，不扫描、不格式化，也不自动重挂载。
 *       控制器 Abort 与 NOR 内部擦写取消不同，不能省略空闲核验。
 */
Platform_StatusTypeDef Platform_Flash_RecoverVolume(void)
{
    if (hplatform_flash_operation_active)
    {
        return PLATFORM_BUSY;
    }
    if (platform_flash_disable_memory_mapped_mode_internal() != PLATFORM_OK)
    {
        return PLATFORM_FLASH_ERROR;
    }
    if (W25Qxx_Quiesce(&hplatform_flash) != W25QXX_OK)
    {
        return PLATFORM_BUSY;
    }
    Platform_StatusTypeDef status =
        platform_flash_map_w25qxx_status(W25Qxx_Recover(&hplatform_flash));
    if (status == PLATFORM_OK)
    {
        platform_flash_ftl_result = FLASH_FTL_NOT_READY;
    }
    return status;
}
