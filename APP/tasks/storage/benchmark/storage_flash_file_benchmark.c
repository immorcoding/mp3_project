/**
 * @file storage_flash_file_benchmark.c
 * @brief Flash 文件端到端测速与校验；APP 只消费 Service 文件能力。
 */
#include "APP/app_config.h"
#include "APP/tasks/storage/benchmark/storage_flash_benchmark_config.h"

#if STORAGE_FLASH_BENCHMARK_ENABLE && STORAGE_FLASH_BENCHMARK_FILE_ENABLE

#include "APP/tasks/storage/benchmark/storage_flash_benchmark.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/log/log_service.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include <stdio.h>

/** @brief 应用层缓冲不直接用于 DMA；静态持有，避免占用 Storage Task 栈。 */
static uint8_t storage_flash_file_buffer[STORAGE_FLASH_BENCHMARK_FILE_CHUNK_BYTES];
/** @brief 一次启动只尝试一次，包括失败，防止反复写入造成磨损。 */
static bool storage_flash_file_attempted;

/**
 * @brief 按文件绝对偏移生成确定性测试图样，检测错位和重复块。
 * @param[in] offset 文件内字节偏移。
 * @return 该位置期望字节值。
 */
static uint8_t storage_flash_file_pattern(uint32_t offset)
{
    return (uint8_t)((offset * 37U) ^ (offset >> 8U) ^ (offset >> 16U) ^ 0xA5U);
}

/**
 * @brief 输出某一完整数据阶段的吞吐与耗时，不使用浮点格式化。
 * @param[in] phase 阶段名称；写阶段包含最终文件同步。
 * @param[in] elapsed 测试阶段累计 RTOS tick 数。
 * @note 日志时间不计入测试；tick 精度不足一个 tick 时显示零速率而不伪造时间。
 */
static void storage_flash_file_log_speed(const char *phase, TickType_t elapsed)
{
    uint64_t ms = (uint64_t)elapsed * 1000ULL / configTICK_RATE_HZ;
    uint64_t speed = elapsed
                         ? (uint64_t)STORAGE_FLASH_BENCHMARK_FILE_TOTAL_BYTES * configTICK_RATE_HZ *
                               100ULL / ((uint64_t)elapsed * 1024ULL * 1024ULL)
                         : 0U;
    char text[144];
    (void)snprintf(text,
                   sizeof(text),
                   "Bench FTL file %s: %lu KiB, %llu ms, %llu.%02llu MiB/s.",
                   phase,
                   (unsigned long)(STORAGE_FLASH_BENCHMARK_FILE_TOTAL_BYTES / 1024U),
                   (unsigned long long)ms,
                   (unsigned long long)(speed / 100U),
                   (unsigned long long)(speed % 100U));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "FLASH", text);
}

/**
 * @brief 分块写入测试图样并同步文件，检查实际写入长度。
 * @param[in] file 本轮新建并保持打开的 Service 文件句柄。
 * @param[out] elapsed 接收写入循环（含图样填充）及最终同步的耗时。
 * @return OK 为全部写入并同步成功；短写返回 ERROR，其余保留 Service 错误。
 * @note 不负责关闭或删除，所有失败统一由入口清理；长阶段采用 RTOS tick 避免 DWT 短周期回绕。
 */
static Service_StatusTypeDef storage_flash_file_write(Service_Filesystem_FileHandleTypeDef file,
                                                      TickType_t *elapsed)
{
    TickType_t start = xTaskGetTickCount();
    for (uint32_t offset = 0U; offset < STORAGE_FLASH_BENCHMARK_FILE_TOTAL_BYTES;
         offset += sizeof(storage_flash_file_buffer))
    {
        for (uint32_t i = 0U; i < sizeof(storage_flash_file_buffer); i++)
        {
            storage_flash_file_buffer[i] = storage_flash_file_pattern(offset + i);
        }
        uint32_t transferred;
        Service_StatusTypeDef status = Service_Filesystem_WriteFile(
            file, storage_flash_file_buffer, sizeof(storage_flash_file_buffer), &transferred);
        if (status != SERVICE_OK || transferred != sizeof(storage_flash_file_buffer))
        {
            return status == SERVICE_OK ? SERVICE_ERROR : status;
        }
    }
    Service_StatusTypeDef status = Service_Filesystem_SyncFile(file);
    *elapsed = xTaskGetTickCount() - start;
    return status;
}

/**
 * @brief 顺序读取整个测试文件，可选择逐字节校验，并确认文件恰好到达 EOF。
 * @param[in] file 新打开且文件指针位于起点的只读句柄。
 * @param[in] verify true 为校验阶段；false 为不含比较开销的读取测速阶段。
 * @param[out] elapsed 接收本阶段读取及可选比较的 tick 数。
 * @return OK 为长度及可选内容符合期望；短读、额外数据或内容不符返回 ERROR。
 * @note 测速与校验各自重新打开文件；不依赖读取阶段残留的 APP 缓冲。
 */
static Service_StatusTypeDef storage_flash_file_read(Service_Filesystem_FileHandleTypeDef file,
                                                     bool verify,
                                                     TickType_t *elapsed)
{
    TickType_t start = xTaskGetTickCount();
    for (uint32_t offset = 0U; offset < STORAGE_FLASH_BENCHMARK_FILE_TOTAL_BYTES;
         offset += sizeof(storage_flash_file_buffer))
    {
        uint32_t transferred;
        Service_StatusTypeDef status = Service_Filesystem_ReadFile(
            file, storage_flash_file_buffer, sizeof(storage_flash_file_buffer), &transferred);
        if (status != SERVICE_OK || transferred != sizeof(storage_flash_file_buffer))
        {
            return status == SERVICE_OK ? SERVICE_ERROR : status;
        }
        if (verify)
        {
            for (uint32_t i = 0U; i < sizeof(storage_flash_file_buffer); i++)
            {
                if (storage_flash_file_buffer[i] != storage_flash_file_pattern(offset + i))
                {
                    return SERVICE_ERROR;
                }
            }
        }
    }
    uint32_t transferred;
    Service_StatusTypeDef status =
        Service_Filesystem_ReadFile(file, storage_flash_file_buffer, 1U, &transferred);
    *elapsed = xTaskGetTickCount() - start;
    return status == SERVICE_OK && transferred ? SERVICE_ERROR : status;
}

/**
 * @brief 在 Flash 挂载后执行一次文件写入、读取测速、独立校验及删除。
 * @note 仅 Storage Task 调用；不直接包含或调用 FatFs/Platform/FTL，不自动格式化。
 *       CREATE_NEW 失败时不删除同名文件；只有本轮成功创建才拥有删除权。
 *       所有失败均进入关闭/删除清理；介质或文件锁故障时明确报告残留，不假报已删除。
 */
void storage_flash_benchmark_run_file(void)
{
    if (storage_flash_file_attempted)
    {
        return;
    }
    storage_flash_file_attempted = true;

    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_StatusTypeDef status;
    TickType_t elapsed = 0U;
    const char *stage = "create";
    bool created = false;
    bool removed = false;

    status = Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                         STORAGE_FLASH_BENCHMARK_FILE_NAME,
                                         SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                         &file);
    if (status != SERVICE_OK)
    {
        goto cleanup;
    }
    created = true;
    stage = "write/sync";
    status = storage_flash_file_write(file, &elapsed);
    if (status != SERVICE_OK)
    {
        goto cleanup;
    }
    stage = "close writer";
    status = Service_Filesystem_CloseFile(file);
    if (status != SERVICE_OK)
    {
        goto cleanup;
    }
    file.Token = 0U;
    storage_flash_file_log_speed("write+sync", elapsed);

    for (uint32_t pass = 0U; pass < 2U; pass++)
    {
        stage = pass ? "open verify" : "open reader";
        status = Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                             STORAGE_FLASH_BENCHMARK_FILE_NAME,
                                             SERVICE_FILESYSTEM_FILE_MODE_READ,
                                             &file);
        if (status != SERVICE_OK)
        {
            goto cleanup;
        }
        stage = pass ? "verify" : "read";
        status = storage_flash_file_read(file, pass != 0U, &elapsed);
        if (status != SERVICE_OK)
        {
            goto cleanup;
        }
        stage = "close reader";
        status = Service_Filesystem_CloseFile(file);
        if (status != SERVICE_OK)
        {
            goto cleanup;
        }
        file.Token = 0U;
        if (!pass)
        {
            storage_flash_file_log_speed("read", elapsed);
        }
    }

cleanup:
    if (file.Token)
    {
        Service_StatusTypeDef close_status = Service_Filesystem_CloseFile(file);
        if (close_status != SERVICE_OK)
        {
            (void)Service_Log_Post(
                SERVICE_LOG_LEVEL_ERROR,
                "FLASH",
                "Bench FTL file close failed; explicit recovery may be required.");
        }
    }
    if (created)
    {
        Service_StatusTypeDef remove_status =
            Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                          STORAGE_FLASH_BENCHMARK_FILE_NAME);
        removed = remove_status == SERVICE_OK;
        if (!removed)
        {
            if (status == SERVICE_OK)
            {
                status = remove_status;
                stage = "delete";
            }
            (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                                   "FLASH",
                                   "Bench FTL file cleanup failed; test file may remain.");
        }
    }
    if (status == SERVICE_OK && removed)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO,
                               "FLASH",
                               "Bench FTL file passed: data verified; test file deleted.");
    }
    else
    {
        char text[128];
        (void)snprintf(text,
                       sizeof(text),
                       "Bench FTL file failed at %s (Service=%u, deleted=%u).",
                       stage,
                       (unsigned)status,
                       (unsigned)removed);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, "FLASH", text);
    }
}
#else
/** @brief 禁用文件测速时保持翻译单元非空，不产生运行时代码。 */
typedef int storage_flash_file_benchmark_disabled_t;
#endif
