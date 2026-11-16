/**
 ******************************************************************************
 * @file    volume_flash.c
 * @brief   Flash 卷的启动流程：执行器初始化、可选基准、挂载、无文件系统时格式化、发布卷就绪（见 volume_flash.h）。
 *
 * @details
 *          卷路径日志（标签 `VOLUME`，常开；串口里标签后面有冒号），正文：
 *          - `flash boot start`：Flash 启动任务开始，`MountFlash` 持 Flash 卷锁的窗口从这一行起。
 *          - `flash ready path=<mount|format> seq=<前>-><后> ms=<n>`：发了卷就绪；`ms` 是从开始到现在的毫秒数
 *            （含 `MountFlash`，实测 3.3 至 8.3 s）。**这一行出现之前就是 `MountFlash` 期间**（R3.5 在这个窗口里插拔卡）。
 *          - `flash boot failed path=<init|mount|format> ms=<n>`：失败，不发卷事件。
 *          原有的 `FLASH` 标签人读日志（Filesystem mounted.、Formatting Flash volume... 等）保持不变。
 ******************************************************************************
 */

#include "APP/tasks/volume/flash/volume_flash.h"
#include "APP/tasks/volume/volume_event.h"
#include "APP/tasks/volume/volume_task_config.h"
#include "APP/app_config.h"

#include <stdint.h>
#include <stdio.h>

#include "Service/filesystem/filesystem_service.h"
#include "Service/log/log_service.h"

#if STORAGE_FLASH_BENCHMARK_ENABLE
#include "APP/tasks/volume/benchmark/storage_flash_benchmark.h"
#include "APP/tasks/volume/benchmark/storage_flash_benchmark_config.h"
#endif

/** @brief Flash 子系统的人读日志标签。 */
static const char volume_flash_log_tag[] = "FLASH";

/** @brief 卷路径日志标签。 */
static const char volume_flash_path_tag[] = "VOLUME";

/**
 * @brief  从 start 到现在经过的毫秒数。
 * @param[in] start 起点 tick。
 * @return 毫秒数（无符号差值，tick 回绕也对）。
 */
static uint32_t volume_flash_elapsed_ms(TickType_t start)
{
    return (uint32_t)(((uint64_t)(TickType_t)(xTaskGetTickCount() - start) * 1000ULL) / (uint64_t)configTICK_RATE_HZ);
}

/**
 * @brief  失败时写一行卷路径日志。
 * @param[in] path 失败发生的阶段：init、mount 或 format。
 * @param[in] start 流程开始的 tick。
 */
static void volume_flash_post_failure(const char *path, TickType_t start)
{
    char text[64];

    (void)snprintf(text, sizeof(text), "flash boot failed path=%s ms=%lu", path, (unsigned long)volume_flash_elapsed_ms(start));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, volume_flash_path_tag, text);
}

/**
 * @brief  成功时发卷就绪并写一行卷路径日志。
 * @param[in] path 走的路径：mount 或 format。
 * @param[in] start 流程开始的 tick。
 */
static void volume_flash_publish_ready(const char *path, TickType_t start)
{
    char text[64];
    const uint32_t before = volume_status(VOLUME_ID_FLASH).Sequence;
    const uint32_t after = volume_publish_ready(VOLUME_ID_FLASH);

    (void)snprintf(text,
                   sizeof(text),
                   "flash ready path=%s seq=%lu->%lu ms=%lu",
                   path,
                   (unsigned long)before,
                   (unsigned long)after,
                   (unsigned long)volume_flash_elapsed_ms(start));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, volume_flash_path_tag, text);
}

/**
 * @brief  Flash 卷的启动流程：初始化执行器，按策略挂载，成功后才发卷就绪。
 * @return 发了卷就绪为真；初始化、挂载或格式化失败为假（都已记日志，且都不发卷事件）。
 * @note   在一次性的 Flash 启动任务里跑，不要在卷任务里调用：`MountFlash` 持 Flash 卷锁 3.3 至 8.3 s。
 *         未格式化（`SERVICE_NO_FILESYSTEM`）时按 `VOLUME_FLASH_AUTO_FORMAT` 决定是否格式化；损坏、不兼容等
 *         其他失败不格式化，保留可诊断状态（STOR-9）。可选基准（默认关闭）：物理基准在执行器绑定后、挂载前，
 *         文件基准在挂载成功之后、发卷就绪之前，别的任务还不会碰 Flash 卷，独占条件成立。
 */
bool volume_flash_boot(void)
{
    const TickType_t start = xTaskGetTickCount();
    Service_StatusTypeDef status;

    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, volume_flash_path_tag, "flash boot start");

    if (Service_Filesystem_InitFlash() != SERVICE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               volume_flash_log_tag,
                               "Flash executor initialization failed.");
        volume_flash_post_failure("init", start);
        return false;
    }

#if STORAGE_FLASH_BENCHMARK_ENABLE
    storage_flash_benchmark_run();
#endif

    status = Service_Filesystem_MountFlash();
    if (status == SERVICE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, volume_flash_log_tag, "Filesystem mounted.");
#if STORAGE_FLASH_BENCHMARK_ENABLE && STORAGE_FLASH_BENCHMARK_FILE_ENABLE
        storage_flash_benchmark_run_file();
#endif
        volume_flash_publish_ready("mount", start);
        return true;
    }

    if (status == SERVICE_NO_FILESYSTEM)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                               volume_flash_log_tag,
                               "No formatted FTL/FAT volume; explicit format required.");
        if (VOLUME_FLASH_AUTO_FORMAT != 0U)
        {
            (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, volume_flash_log_tag, "Formatting Flash volume...");
            if (Service_Filesystem_FormatAndMountFlash() != SERVICE_OK)
            {
                (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                                       volume_flash_log_tag,
                                       "Flash volume format failed.");
                volume_flash_post_failure("format", start);
                return false;
            }
            (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, volume_flash_log_tag, "Flash volume formatted and mounted.");
            volume_flash_publish_ready("format", start);
            return true;
        }
    }

    (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                           volume_flash_log_tag,
                           "Volume unavailable; no automatic format.");
    volume_flash_post_failure("mount", start);
    return false;
}
