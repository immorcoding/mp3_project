/**
  ******************************************************************************
  * @file    storage_sd_benchmark.c
  * @brief   Storage Task 中执行的 SD 文件系统端到端读写测速与完整性校验。
  *
  * @details
  *          本测试经由 Filesystem Service 文件接口、DiskIO、同步 DMA
  *          执行器和 Platform SD 访问介质，测量当前产品实际可获得的顺序文件吞吐量。
  *          测试文件、数据量和日志属于 APP 诊断逻辑，不属于 Filesystem Service。
  *          吞吐量测试完成后另行读取并校验数据，避免 CPU 校验工作影响读速结果。
  ******************************************************************************
  */
#include "APP/app_config.h"
#if STORAGE_SD_BENCHMARK_ENABLE
/* Includes ------------------------------------------------------------------*/
#include "APP/tasks/storage/benchmark/storage_sd_benchmark.h"
#include "APP/tasks/storage/benchmark/storage_sd_benchmark_config.h"

#include <stdint.h>
#include <stdio.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/log/log_service.h"

/** @brief 测试使用的相对路径；完整性校验通过后会删除该文件。 */
static const char storage_sd_benchmark_path[] = "__sd_rw_bench.bin";

/** @brief 测试应用层缓冲区不直接交给 DMA，因此无需 DMA 段或 Cache 对齐。 */
static uint8_t storage_sd_benchmark_buffer[STORAGE_SD_BENCHMARK_CHUNK_BYTES];

/** @brief 最终日志文本缓存，避免测速任务的局部格式化缓冲区占用栈空间。 */
static char storage_sd_benchmark_log_text[128];

/** @brief 防止同一张已挂载 SD 卡被重复执行测速。 */
static bool storage_sd_benchmark_completed;

/** @brief 防止异常重入导致同一个测试文件被并发访问。 */
static bool storage_sd_benchmark_running;

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  初始化每个测试缓冲区共用的固定有效载荷模式。
  * @note   前四字节由 storage_sd_benchmark_set_sequence() 在每次写入前覆盖，
  *         后续读取校验可同时发现顺序错误和有效载荷损坏。
  */
static void storage_sd_benchmark_prepare_pattern(void)
{
    uint32_t index;

    for (index = STORAGE_SD_BENCHMARK_SEQUENCE_BYTES;
         index < STORAGE_SD_BENCHMARK_CHUNK_BYTES;
         index++)
    {
        storage_sd_benchmark_buffer[index] =
            (uint8_t)((index * 37U) ^ 0xA5U);
    }
}

/**
  * @brief  把当前测试块序号写入缓冲区头部，采用显式小端字节序。
  * @param  sequence 当前从零开始的应用层块序号。
  */
static void storage_sd_benchmark_set_sequence(uint32_t sequence)
{
    storage_sd_benchmark_buffer[0] = (uint8_t)(sequence & 0xFFU);
    storage_sd_benchmark_buffer[1] = (uint8_t)((sequence >> 8U) & 0xFFU);
    storage_sd_benchmark_buffer[2] = (uint8_t)((sequence >> 16U) & 0xFFU);
    storage_sd_benchmark_buffer[3] = (uint8_t)((sequence >> 24U) & 0xFFU);
}

/**
  * @brief  校验当前应用层缓冲区是否仍保持指定块序号和固定有效载荷。
  * @param  expected_sequence 期望的从零开始块序号。
  * @retval true 缓冲区内容与写入时完全一致。
  * @retval false 块序号或固定有效载荷存在不一致。
  */
static bool storage_sd_benchmark_verify_buffer(uint32_t expected_sequence)
{
    uint32_t actual_sequence;
    uint32_t index;

    actual_sequence = (uint32_t)storage_sd_benchmark_buffer[0] |
                      ((uint32_t)storage_sd_benchmark_buffer[1] << 8U) |
                      ((uint32_t)storage_sd_benchmark_buffer[2] << 16U) |
                      ((uint32_t)storage_sd_benchmark_buffer[3] << 24U);
    if (actual_sequence != expected_sequence)
    {
        return false;
    }

    for (index = STORAGE_SD_BENCHMARK_SEQUENCE_BYTES;
         index < STORAGE_SD_BENCHMARK_CHUNK_BYTES;
         index++)
    {
        if (storage_sd_benchmark_buffer[index] !=
            (uint8_t)((index * 37U) ^ 0xA5U))
        {
            return false;
        }
    }

    return true;
}

/**
  * @brief  将 FreeRTOS Tick 数换算为整毫秒。
  * @param  elapsed_ticks 已按无符号减法取得的连续 Tick 数。
  * @return 向下取整的毫秒数。
  * @note   当前 configTICK_RATE_HZ 为 1000；仍保留通用换算，避免把该配置写死。
  */
static uint64_t storage_sd_benchmark_ticks_to_ms(uint32_t elapsed_ticks)
{
    return ((uint64_t)elapsed_ticks * 1000ULL) / configTICK_RATE_HZ;
}

/**
  * @brief  计算以百分之一 MiB/s 表示的顺序吞吐量。
  * @param  elapsed_ticks 当前阶段消耗的连续 Tick 数。
  * @return 速度乘以 100；零 Tick 返回零，避免除零。
  */
static uint64_t storage_sd_benchmark_mib_per_second_x100(uint32_t elapsed_ticks)
{
    if (elapsed_ticks == 0U)
    {
        return 0U;
    }

    return ((uint64_t)STORAGE_SD_BENCHMARK_TOTAL_BYTES *
            (uint64_t)configTICK_RATE_HZ * 100ULL) /
           ((uint64_t)elapsed_ticks * 1024ULL * 1024ULL);
}

/**
  * @brief  将只用于显示的无符号 64 位数安全地缩小为 32 位数。
  * @param  value 待输出的非负计算结果。
  * @return 可由当前轻量 snprintf 的 %lu 格式输出的值；超过范围时饱和为 UINT32_MAX。
  * @note   吞吐量的中间计算仍使用 uint64_t，避免乘法在测速逻辑中提前溢出。
  */
static uint32_t storage_sd_benchmark_display_u32(uint64_t value)
{
    return (value > UINT32_MAX) ? UINT32_MAX : (uint32_t)value;
}

/**
  * @brief  删除上次保留的测试文件。
  * @return SERVICE_OK 文件已删除或此前不存在；其他值表示清理失败。
  */
static Service_StatusTypeDef storage_sd_benchmark_remove_previous_file(void)
{
    return Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, storage_sd_benchmark_path);
}

/**
  * @brief  创建测试文件、顺序写入固定有效载荷并同步到 SD 卡。
  * @param  elapsed_ticks 接收从首次 WriteFile 到 SyncFile 完成的连续 Tick 数。
  * @retval SERVICE_OK 所有测试块均已写入且同步成功。
  * @note   计时不包含删除旧文件、打开和日志；计时包含每次写入与最终同步。
  */
static Service_StatusTypeDef storage_sd_benchmark_write_file(uint32_t *elapsed_ticks)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_StatusTypeDef status;
    TickType_t start_tick;
    uint32_t sequence;

    if (elapsed_ticks == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }

    status = Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                         storage_sd_benchmark_path,
                                         SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                         &file);
    if (status != SERVICE_OK)
    {
        return status;
    }

    start_tick = xTaskGetTickCount();
    for (sequence = 0U; sequence < STORAGE_SD_BENCHMARK_CHUNK_COUNT; sequence++)
    {
        uint32_t transferred = 0U;

        storage_sd_benchmark_set_sequence(sequence);
        status = Service_Filesystem_WriteFile(file,
                                              storage_sd_benchmark_buffer,
                                              STORAGE_SD_BENCHMARK_CHUNK_BYTES,
                                              &transferred);
        if ((status != SERVICE_OK) || (transferred != STORAGE_SD_BENCHMARK_CHUNK_BYTES))
        {
            if (status == SERVICE_OK)
            {
                status = SERVICE_ERROR;
            }
            break;
        }
    }

    if (status == SERVICE_OK)
    {
        status = Service_Filesystem_SyncFile(file);
    }

    *elapsed_ticks = (uint32_t)(xTaskGetTickCount() - start_tick);

    if (Service_Filesystem_CloseFile(file) != SERVICE_OK)
    {
        status = (status == SERVICE_OK) ? SERVICE_ERROR : status;
    }

    return status;
}

/**
  * @brief  顺序读取完整测试文件，并测量文件读取有效吞吐量。
  * @param  elapsed_ticks 接收从首次 ReadFile 到最后一次 ReadFile 完成的连续 Tick 数。
  * @retval SERVICE_OK 读取到的所有应用层块都具有预期长度。
  * @note   本阶段不在循环内校验有效载荷。
  */
static Service_StatusTypeDef storage_sd_benchmark_read_file(uint32_t *elapsed_ticks)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_StatusTypeDef status;
    TickType_t start_tick;
    uint32_t sequence;

    if (elapsed_ticks == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }

    status = Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                         storage_sd_benchmark_path,
                                         SERVICE_FILESYSTEM_FILE_MODE_READ,
                                         &file);
    if (status != SERVICE_OK)
    {
        return status;
    }

    start_tick = xTaskGetTickCount();
    for (sequence = 0U; sequence < STORAGE_SD_BENCHMARK_CHUNK_COUNT; sequence++)
    {
        uint32_t transferred = 0U;

        status = Service_Filesystem_ReadFile(file,
                                             storage_sd_benchmark_buffer,
                                             STORAGE_SD_BENCHMARK_CHUNK_BYTES,
                                             &transferred);
        if ((status != SERVICE_OK) || (transferred != STORAGE_SD_BENCHMARK_CHUNK_BYTES))
        {
            if (status == SERVICE_OK)
            {
                status = SERVICE_ERROR;
            }
            break;
        }
    }

    *elapsed_ticks = (uint32_t)(xTaskGetTickCount() - start_tick);

    if (Service_Filesystem_CloseFile(file) != SERVICE_OK)
    {
        status = (status == SERVICE_OK) ? SERVICE_ERROR : status;
    }

    return status;
}

/**
  * @brief  重新顺序读取测试文件并校验全部块序号与固定有效载荷。
  * @param  data_matched 接收内容是否与写入模式完全一致。
  * @retval SERVICE_OK 文件可完整读取；此时由 data_matched 区分内容是否一致。
  */
static Service_StatusTypeDef storage_sd_benchmark_verify_file(bool *data_matched)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_StatusTypeDef status;
    uint32_t sequence;

    if (data_matched == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }

    *data_matched = false;
    status = Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                         storage_sd_benchmark_path,
                                         SERVICE_FILESYSTEM_FILE_MODE_READ,
                                         &file);
    if (status != SERVICE_OK)
    {
        return status;
    }

    for (sequence = 0U; sequence < STORAGE_SD_BENCHMARK_CHUNK_COUNT; sequence++)
    {
        uint32_t transferred = 0U;

        status = Service_Filesystem_ReadFile(file,
                                             storage_sd_benchmark_buffer,
                                             STORAGE_SD_BENCHMARK_CHUNK_BYTES,
                                             &transferred);
        if ((status != SERVICE_OK) || (transferred != STORAGE_SD_BENCHMARK_CHUNK_BYTES))
        {
            if (status == SERVICE_OK)
            {
                status = SERVICE_ERROR;
            }
            break;
        }

        if (!storage_sd_benchmark_verify_buffer(sequence))
        {
            break;
        }
    }

    if (Service_Filesystem_CloseFile(file) != SERVICE_OK)
    {
        status = (status == SERVICE_OK) ? SERVICE_ERROR : status;
    }

    if ((status == SERVICE_OK) && (sequence == STORAGE_SD_BENCHMARK_CHUNK_COUNT))
    {
        *data_matched = true;
    }

    return status;
}

/**
  * @brief  执行当前 SD 文件系统端到端读写测速与完整性校验。
  * @retval true 写入、同步、顺序读取和无计时完整性校验均成功。
  * @retval false 测试前清理、读写、校验或最终清理失败。
  * @note   调用者必须已经在 Storage Task 中成功挂载当前 SD 卷；成功结果只执行一次。
  */
bool storage_sd_benchmark_run(void)
{
    Service_StatusTypeDef status;
    uint32_t elapsed_ticks;
    uint32_t elapsed_ms;
    uint32_t speed_x100;
    bool data_matched;
    bool success = false;

    if (storage_sd_benchmark_completed)
    {
        return true;
    }

    if (storage_sd_benchmark_running)
    {
        return false;
    }

    storage_sd_benchmark_running = true;

    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO,
                           "SD",
                           "Bench start: read/write 64 MiB, chunk 32 KiB.");

    status = storage_sd_benchmark_remove_previous_file();
    if (status != SERVICE_OK)
    {
        (void)snprintf(storage_sd_benchmark_log_text,
                       sizeof(storage_sd_benchmark_log_text),
                       "Bench cleanup failed: Service=%u.",
                       (unsigned)status);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, "SD", storage_sd_benchmark_log_text);
        goto exit;
    }

    storage_sd_benchmark_prepare_pattern();
    status = storage_sd_benchmark_write_file(&elapsed_ticks);
    if (status != SERVICE_OK)
    {
        (void)snprintf(storage_sd_benchmark_log_text,
                       sizeof(storage_sd_benchmark_log_text),
                       "Bench write failed: Service=%u.",
                       (unsigned)status);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, "SD", storage_sd_benchmark_log_text);
        goto exit;
    }

    elapsed_ms = storage_sd_benchmark_display_u32(
        storage_sd_benchmark_ticks_to_ms(elapsed_ticks));
    speed_x100 = storage_sd_benchmark_display_u32(
        storage_sd_benchmark_mib_per_second_x100(elapsed_ticks));
    (void)snprintf(storage_sd_benchmark_log_text,
                   sizeof(storage_sd_benchmark_log_text),
                   "Bench write: 64 MiB, %lu ms, %lu.%02lu MiB/s.",
                   (unsigned long)elapsed_ms,
                   (unsigned long)(speed_x100 / 100U),
                   (unsigned long)(speed_x100 % 100U));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "SD", storage_sd_benchmark_log_text);

    status = storage_sd_benchmark_read_file(&elapsed_ticks);
    if (status != SERVICE_OK)
    {
        (void)snprintf(storage_sd_benchmark_log_text,
                       sizeof(storage_sd_benchmark_log_text),
                       "Bench read failed: Service=%u.",
                       (unsigned)status);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, "SD", storage_sd_benchmark_log_text);
        goto exit;
    }

    elapsed_ms = storage_sd_benchmark_display_u32(
        storage_sd_benchmark_ticks_to_ms(elapsed_ticks));
    speed_x100 = storage_sd_benchmark_display_u32(
        storage_sd_benchmark_mib_per_second_x100(elapsed_ticks));
    (void)snprintf(storage_sd_benchmark_log_text,
                   sizeof(storage_sd_benchmark_log_text),
                   "Bench read: 64 MiB, %lu ms, %lu.%02lu MiB/s.",
                   (unsigned long)elapsed_ms,
                   (unsigned long)(speed_x100 / 100U),
                   (unsigned long)(speed_x100 % 100U));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "SD", storage_sd_benchmark_log_text);

    status = storage_sd_benchmark_verify_file(&data_matched);
    if (status != SERVICE_OK)
    {
        (void)snprintf(storage_sd_benchmark_log_text,
                       sizeof(storage_sd_benchmark_log_text),
                       "Bench verify read failed: Service=%u.",
                       (unsigned)status);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, "SD", storage_sd_benchmark_log_text);
        goto exit;
    }

    if (!data_matched)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               "SD",
                               "Bench verify failed: data mismatch.");
        goto exit;
    }

    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "SD", "Bench verify: 64 MiB passed.");

    status = storage_sd_benchmark_remove_previous_file();
    if (status != SERVICE_OK)
    {
        (void)snprintf(storage_sd_benchmark_log_text,
                       sizeof(storage_sd_benchmark_log_text),
                       "Bench final cleanup failed: Service=%u.",
                       (unsigned)status);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, "SD", storage_sd_benchmark_log_text);
        goto exit;
    }

    storage_sd_benchmark_completed = true;
    success = true;
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "SD", "Bench complete.");

exit:
    storage_sd_benchmark_running = false;
    return success;
}
#else

/* 保持基准开关关闭时的翻译单元非空，不引入任何运行时代码。 */
typedef int storage_sd_benchmark_disabled_translation_unit_t;

#endif /* STORAGE_SD_BENCHMARK_ENABLE */

