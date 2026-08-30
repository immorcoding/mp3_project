/**
  ******************************************************************************
  * @file    storage_flash_benchmark.c
  * @brief   Storage Task 中外部 W25Q256 的读取测速与保留扇区自检。
  *
  * @details
  *          本 Module 在 APP 层组织读取规模、FreeRTOS 时间采样和日志；实际物理
  *          读取仍只经 Platform Flash Interface 完成。顺序读取故意不解释读回
  *          数据，因此可安全读取尚未分区的任意数组区域；启用破坏性开关后，
  *          Platform 仅可操作 ADR-0009 预留的首尾 4 KiB 自检扇区。
  ******************************************************************************
  */

#include "APP/app_config.h"

#if STORAGE_FLASH_BENCHMARK_ENABLE

/* Includes ------------------------------------------------------------------*/
#include "APP/tasks/storage/benchmark/storage_flash_benchmark.h"
#include "APP/tasks/storage/benchmark/storage_flash_benchmark_config.h"
#include "APP/tasks/storage/storage_flash.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Platform/flash/platform_flash.h"
#include "Service/log/log_service.h"

/* Private variables ---------------------------------------------------------*/
/**
 * @brief 顺序读取和破坏性自检共用的 4 KiB AXI SRAM 临时缓冲区。
 * @note  该区由 Linker Script 放入 `.flash_write_buffer`。轮询读取和 MDMA
 *        读取共用它；MDMA 开始前 Adapter 清理 Cache，完成后在任务上下文失效
 *        Cache，因而这块缓冲区在同一时间只能服务一条 Flash 读取。
 */
static uint8_t storage_flash_benchmark_buffer[STORAGE_FLASH_BENCHMARK_READ_CHUNK_BYTES]
    __attribute__((section(".flash_write_buffer"), aligned(PLATFORM_DMA_BUFFER_ALIGNMENT)));

/** @brief 本 Module 的稳定日志标签。 */
static const char storage_flash_benchmark_log_tag[] = "FLASH";

/** @brief 防止同一上电周期重复占用 Storage Task 进行测速。 */
static bool storage_flash_benchmark_completed;

/** @brief 防止未来从多个启动路径重入基准入口。 */
static bool storage_flash_benchmark_running;

/* Private functions ---------------------------------------------------------*/
/**
 * @brief  把 FreeRTOS Tick 间隔换算为毫秒。
 * @param  elapsed_ticks 本次测速累计的 Tick 数。
 * @retval 向下取整的毫秒数。
 */
static uint64_t storage_flash_benchmark_ticks_to_ms(uint32_t elapsed_ticks)
{
    return ((uint64_t)elapsed_ticks * 1000ULL) / configTICK_RATE_HZ;
}

/**
 * @brief  根据本次固定读取量与 Tick 间隔计算百分之一 MiB/s。
 * @param  elapsed_ticks 本次测速累计的 Tick 数。
 * @retval 吞吐率乘以 100；零 Tick 时返回零。
 */
static uint64_t storage_flash_benchmark_read_mib_per_second_x100(
    uint32_t elapsed_ticks)
{
    if (elapsed_ticks == 0U)
    {
        return 0U;
    }

    return ((uint64_t)STORAGE_FLASH_BENCHMARK_READ_TOTAL_BYTES *
            configTICK_RATE_HZ *
            100ULL) /
           ((uint64_t)elapsed_ticks * 1024ULL * 1024ULL);
}

/**
 * @brief  顺序读取配置的物理数组范围并采集端到端耗时。
 * @param  elapsed_ticks 接收累计 Tick 的有效地址。
 * @retval true 全部 QSPI 读取成功。
 * @retval false 参数无效或任一 Platform Flash 读取失败。
 * @note   计时范围包含每块 0xEC 指令、地址、模式字节、dummy cycle、HAL 轮询
 *         和数据搬运；中断与调度带来的延迟同样保留在真实产品吞吐结果中。
 */
static bool storage_flash_benchmark_read_poll(uint32_t *elapsed_ticks)
{
    uint32_t address = STORAGE_FLASH_BENCHMARK_READ_START_ADDRESS;
    const TickType_t start_tick = xTaskGetTickCount();

    if (elapsed_ticks == NULL)
    {
        return false;
    }

    for (uint32_t offset = 0U;
         offset < STORAGE_FLASH_BENCHMARK_READ_TOTAL_BYTES;
         offset += STORAGE_FLASH_BENCHMARK_READ_CHUNK_BYTES)
    {
        if (Platform_Flash_ReadArray(address,
                                     storage_flash_benchmark_buffer,
                                     sizeof(storage_flash_benchmark_buffer)) != PLATFORM_OK)
        {
            return false;
        }

        address += STORAGE_FLASH_BENCHMARK_READ_CHUNK_BYTES;
    }

    *elapsed_ticks = (uint32_t)(xTaskGetTickCount() - start_tick);
    return true;
}

/**
 * @brief  顺序执行由 MDMA 搬运的 0xEC 数组读取并采集端到端耗时。
 * @param  elapsed_ticks 接收累计 Tick 的有效地址。
 * @retval true 所有 MDMA 读取都在有界时限内完成并通过 Platform 收尾。
 * @retval false 参数无效、启动失败、未收到 IRQ 通知，或 QSPI/MDMA 收尾失败。
 * @note   每块的 QSPI IRQ 订阅、遗留通知清理、等待与普通上下文收尾均由
 *         storage_flash 协调 Module 执行。回调可能在启动函数返回前到达，但任务
 *         通知是计数型，后续等待仍能立即取得完成事件。
 */
static bool storage_flash_benchmark_read_mdma(uint32_t *elapsed_ticks)
{
    uint32_t address = STORAGE_FLASH_BENCHMARK_READ_START_ADDRESS;
    const TickType_t start_tick = xTaskGetTickCount();

    if (elapsed_ticks == NULL)
    {
        return false;
    }

    for (uint32_t offset = 0U;
         offset < STORAGE_FLASH_BENCHMARK_READ_TOTAL_BYTES;
         offset += STORAGE_FLASH_BENCHMARK_READ_CHUNK_BYTES)
    {
        if (!storage_flash_read_array(address,
                                      storage_flash_benchmark_buffer,
                                      sizeof(storage_flash_benchmark_buffer),
                                      STORAGE_FLASH_BENCHMARK_MDMA_TIMEOUT_MS))
        {
            return false;
        }

        address += STORAGE_FLASH_BENCHMARK_READ_CHUNK_BYTES;
    }

    *elapsed_ticks = (uint32_t)(xTaskGetTickCount() - start_tick);
    return true;
}

#if STORAGE_FLASH_BENCHMARK_PROGRAM_ENABLE
/**
 * @brief  使用当前 MDMA 配置读回并校验刚完成轮询自检的两个保留扇区。
 * @retval true 首、尾 4 KiB 均经 MDMA 读取、普通上下文 Cache 收尾并逐字节校验。
 * @retval false 任一诊断区域无法启动、完成、收尾或匹配预期图样。
 * @note   调用者必须已由 storage_flash_init() 绑定 Storage Task 的长期传输
 *         通知，且刚由 Platform_Flash_RunDiagnostic() 成功写入并通过同步读回
 *         验证。APP 不掌握保留扇区的物理地址或图样，只向 Platform 传递区域语义。
 */
static bool storage_flash_benchmark_verify_diagnostic_with_mdma(void)
{
    static const Platform_Flash_DiagnosticRegionTypeDef regions[] = {
        PLATFORM_FLASH_DIAGNOSTIC_REGION_HEAD,
        PLATFORM_FLASH_DIAGNOSTIC_REGION_TAIL
    };

    for (uint32_t index = 0U;
         index < (sizeof(regions) / sizeof(regions[0]));
         ++index)
    {
        if (!storage_flash_read_diagnostic(
                regions[index],
                storage_flash_benchmark_buffer,
                sizeof(storage_flash_benchmark_buffer),
                STORAGE_FLASH_BENCHMARK_MDMA_TIMEOUT_MS))
        {
            return false;
        }

        if (Platform_Flash_VerifyDiagnosticReadBuffer(
                regions[index],
                storage_flash_benchmark_buffer,
                sizeof(storage_flash_benchmark_buffer)) != PLATFORM_OK)
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief  执行一次首尾自检扇区破坏性诊断并投递完整性结果。
 * @retval true 两个保留扇区均已写入一次，并分别经轮询和 MDMA 读回逐字节校验。
 * @retval false 擦除、页编程、任一路读回或图样校验任一阶段失败。
 * @note   物理地址、图样和所有实际擦写均收敛在 Platform Flash；APP 只提供
 *         4 KiB 缓冲、MDMA 等待和日志策略，不能指定或修改任意 W25Q256 物理区域。
 */
static bool storage_flash_benchmark_run_destructive_diagnostic(void)
{
    Platform_Flash_DiagnosticsTypeDef diagnostics;
    char text[160];

    if (Platform_Flash_RunDiagnostic(storage_flash_benchmark_buffer,
                                     sizeof(storage_flash_benchmark_buffer),
                                     &diagnostics) != PLATFORM_OK)
    {
        (void)snprintf(text,
                       sizeof(text),
                       "Self-test failed: stage=%lu, addr=0x%08lX, expected=0x%02X, actual=0x%02X.",
                       (unsigned long)diagnostics.FailedStage,
                       (unsigned long)diagnostics.FailureAddress,
                       (unsigned int)diagnostics.ExpectedValue,
                       (unsigned int)diagnostics.ActualValue);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_flash_benchmark_log_tag,
                               text);
        return false;
    }

    if (!storage_flash_benchmark_verify_diagnostic_with_mdma())
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_flash_benchmark_log_tag,
                               "Self-test MDMA failed: head/tail read or verify.");
        return false;
    }

    (void)snprintf(text,
                   sizeof(text),
                   "Self-test passed: poll + MDMA, head/tail, %lu KiB verified.",
                   (unsigned long)(diagnostics.TestedBytes / 1024U));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO,
                           storage_flash_benchmark_log_tag,
                           text);
    return true;
}
#endif /* STORAGE_FLASH_BENCHMARK_PROGRAM_ENABLE */

/* Exported functions --------------------------------------------------------*/
/**
 * @brief  执行一次外部 W25Q256 顺序读取基准并将结果投递到 Log Service。
 * @note   该函数只应在 Storage Task 的启动早期调用。它不读取或修改任何 FTL、
 *         文件系统或用户数据元信息；物理数组内容也不会改变。
 */
void storage_flash_benchmark_run(void)
{
    uint32_t elapsed_ticks;
    uint32_t elapsed_ms;
    uint32_t speed_x100;
    char text[128];

    if (storage_flash_benchmark_completed || storage_flash_benchmark_running)
    {
        return;
    }

    storage_flash_benchmark_running = true;

    if (!storage_flash_benchmark_read_poll(&elapsed_ticks))
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_flash_benchmark_log_tag,
                               "Bench poll read failed.");
        goto complete;
    }

    elapsed_ms = (uint32_t)storage_flash_benchmark_ticks_to_ms(elapsed_ticks);
    speed_x100 = (uint32_t)storage_flash_benchmark_read_mib_per_second_x100(elapsed_ticks);
    (void)snprintf(text,
                   sizeof(text),
                   "Bench poll read: %lu KiB, %lu ms, %lu.%02lu MiB/s.",
                   (unsigned long)(STORAGE_FLASH_BENCHMARK_READ_TOTAL_BYTES / 1024U),
                   (unsigned long)elapsed_ms,
                   (unsigned long)(speed_x100 / 100U),
                   (unsigned long)(speed_x100 % 100U));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO,
                           storage_flash_benchmark_log_tag,
                           text);

    if (!storage_flash_benchmark_read_mdma(&elapsed_ticks))
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_flash_benchmark_log_tag,
                               "Bench MDMA read failed.");
        goto complete;
    }

    elapsed_ms = (uint32_t)storage_flash_benchmark_ticks_to_ms(elapsed_ticks);
    speed_x100 = (uint32_t)storage_flash_benchmark_read_mib_per_second_x100(elapsed_ticks);
    (void)snprintf(text,
                   sizeof(text),
                   "Bench MDMA read: %lu KiB, %lu ms, %lu.%02lu MiB/s.",
                   (unsigned long)(STORAGE_FLASH_BENCHMARK_READ_TOTAL_BYTES / 1024U),
                   (unsigned long)elapsed_ms,
                   (unsigned long)(speed_x100 / 100U),
                   (unsigned long)(speed_x100 % 100U));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO,
                           storage_flash_benchmark_log_tag,
                           text);

#if STORAGE_FLASH_BENCHMARK_PROGRAM_ENABLE
    if (!storage_flash_benchmark_run_destructive_diagnostic())
    {
        goto complete;
    }
#endif

complete:
    storage_flash_benchmark_completed = true;
    storage_flash_benchmark_running = false;
}

#else

/* 保持基准开关关闭时的翻译单元非空，不引入任何运行时代码。 */
typedef int storage_flash_benchmark_disabled_translation_unit_t;

#endif /* STORAGE_FLASH_BENCHMARK_ENABLE */
