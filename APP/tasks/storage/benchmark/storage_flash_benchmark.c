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

#include <stdint.h>
#include <stdio.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Platform/flash/platform_flash.h"
#include "Service/log/log_service.h"

/* Private variables ---------------------------------------------------------*/
/**
 * @brief 顺序读取和破坏性自检共用的 4 KiB AXI SRAM 临时缓冲区。
 * @note  该区由 Linker Script 放入 `.flash_write_buffer`；目前使用同步轮询，
 *        未来 QSPI DMA 也必须复用这块 32-byte 对齐的专属缓冲区并在使用前完整写入。
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
static bool storage_flash_benchmark_read(uint32_t *elapsed_ticks)
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

#if STORAGE_FLASH_BENCHMARK_PROGRAM_ENABLE
/**
 * @brief  执行一次首尾自检扇区破坏性诊断并投递完整性和测速日志。
 * @retval true 两个保留扇区均已擦除、写满、读回且逐字节校验通过。
 * @retval false DWT、擦除、页编程、读回或图样校验任一阶段失败。
 * @note   物理地址、图样和所有实际擦写均收敛在 Platform Flash；APP 只提供
 *         4 KiB 缓冲与日志策略，不能指定或修改任意 W25Q256 物理区域。
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

    (void)snprintf(text,
                   sizeof(text),
                   "Self-test passed: head/tail, %lu KiB verified.",
                   (unsigned long)(diagnostics.TestedBytes / 1024U));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO,
                           storage_flash_benchmark_log_tag,
                           text);

    (void)snprintf(text,
                   sizeof(text),
                   "Bench erase: %lu KiB, %lu ms.",
                   (unsigned long)(diagnostics.TestedBytes / 1024U),
                   (unsigned long)diagnostics.EraseElapsedMilliseconds);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO,
                           storage_flash_benchmark_log_tag,
                           text);

    (void)snprintf(text,
                   sizeof(text),
                   "Bench write: %lu KiB, %lu ms, %lu.%02lu MiB/s.",
                   (unsigned long)(diagnostics.TestedBytes / 1024U),
                   (unsigned long)diagnostics.ProgramElapsedMilliseconds,
                   (unsigned long)(diagnostics.ProgramSpeedMiBPerSecondX100 / 100U),
                   (unsigned long)(diagnostics.ProgramSpeedMiBPerSecondX100 % 100U));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO,
                           storage_flash_benchmark_log_tag,
                           text);

    (void)snprintf(text,
                   sizeof(text),
                   "Bench verify read: %lu KiB, %lu ms, %lu.%02lu MiB/s.",
                   (unsigned long)(diagnostics.TestedBytes / 1024U),
                   (unsigned long)diagnostics.ReadElapsedMilliseconds,
                   (unsigned long)(diagnostics.ReadSpeedMiBPerSecondX100 / 100U),
                   (unsigned long)(diagnostics.ReadSpeedMiBPerSecondX100 % 100U));
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

    if (!storage_flash_benchmark_read(&elapsed_ticks))
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_flash_benchmark_log_tag,
                               "Bench read failed.");
        storage_flash_benchmark_running = false;
        return;
    }

    elapsed_ms = (uint32_t)storage_flash_benchmark_ticks_to_ms(elapsed_ticks);
    speed_x100 = (uint32_t)storage_flash_benchmark_read_mib_per_second_x100(elapsed_ticks);
    (void)snprintf(text,
                   sizeof(text),
                   "Bench read: %lu KiB, %lu ms, %lu.%02lu MiB/s.",
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
        storage_flash_benchmark_completed = true;
        storage_flash_benchmark_running = false;
        return;
    }
#endif

    storage_flash_benchmark_completed = true;
    storage_flash_benchmark_running = false;
}

#else

/* 保持基准开关关闭时的翻译单元非空，不引入任何运行时代码。 */
typedef int storage_flash_benchmark_disabled_translation_unit_t;

#endif /* STORAGE_FLASH_BENCHMARK_ENABLE */
