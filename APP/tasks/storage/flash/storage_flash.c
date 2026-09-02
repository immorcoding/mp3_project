/**
 * @file storage_flash.c
 * @brief Storage Task 的 Flash 执行器启动、挂载策略与空闲回收。
 *
 * @details
 *          本模块决定何时初始化执行器、何时跑物理/文件基准、如何对待未格式化
 *          卷，以及空闲回收失败时如何告警。QSPI 等待与块传输仍由 Filesystem
 *          Service 私有执行器完成。启动路径绝不自动格式化。
 *          仅接受当前正在运行的 Storage Task 句柄；其它任务调用直接拒绝。
 */

#include "APP/tasks/storage/flash/storage_flash.h"
#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/storage/benchmark/storage_flash_benchmark.h"
#include "APP/app_config.h"

#include "Service/filesystem/filesystem_service.h"
#include "Service/log/log_service.h"

#include <stdbool.h>

/** @brief Storage Task 发送 Flash 子系统日志时使用的稳定标签。 */
static const char storage_flash_log_tag[] = "FLASH";

/** @brief 已接受的 Storage Task 句柄；未通过 init 校验前拒绝后续调用。 */
static TaskHandle_t storage_flash_owner_task;

/**
 * @brief 确认调用者就是传入的 Storage Task。
 * @param[in] task_handle 声称的 Storage Task 句柄。
 * @return true 句柄有效且等于当前任务。
 */
static bool storage_flash_is_storage_task(TaskHandle_t task_handle)
{
    return (task_handle != NULL) && (task_handle == xTaskGetCurrentTaskHandle());
}

/**
 * @brief 初始化 Flash 执行器，按策略挂载已有卷，并在成功后运行可选基准。
 * @param[in] task_handle 当前 Storage Task 的有效任务句柄。
 * @note 必须在该任务的普通上下文调用。句柄为空、或与当前任务不符时拒绝。
 *       未格式化只记录告警，不调用格式化。物理基准需要执行器已经绑定；
 *       文件基准需要 FAT 已经挂载。
 */
void storage_flash_init(TaskHandle_t task_handle)
{
    Service_StatusTypeDef status;

    if (!storage_flash_is_storage_task(task_handle))
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_flash_log_tag,
                               "Storage task handle is invalid.");
        return;
    }

    storage_flash_owner_task = task_handle;

    if (Service_Filesystem_InitFlash(STORAGE_NOTIFY_FLASH_OPERATION) != SERVICE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_flash_log_tag,
                               "Flash executor initialization failed.");
        return;
    }

#if STORAGE_FLASH_BENCHMARK_ENABLE
    storage_flash_benchmark_run();
#endif

    status = Service_Filesystem_MountFlash();
    if (status == SERVICE_OK)
    {
        (void)Service_Log_Post(
            SERVICE_LOG_LEVEL_INFO, storage_flash_log_tag, "Filesystem mounted.");
#if STORAGE_FLASH_BENCHMARK_ENABLE && STORAGE_FLASH_BENCHMARK_FILE_ENABLE
        storage_flash_benchmark_run_file();
#endif
        return;
    }

    if (status == SERVICE_NO_FILESYSTEM)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                               storage_flash_log_tag,
                               "No formatted FTL/FAT volume; explicit format required.");
#if STORAGE_FLASH_AUTO_FORMAT
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO,
                               storage_flash_log_tag,
                               "Formatting Flash volume...");
        if (Service_Filesystem_FormatAndMountFlash() != SERVICE_OK)
        {
            (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                                   storage_flash_log_tag,
                                   "Flash volume format failed.");
            return;
        }
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO,
                               storage_flash_log_tag,
                               "Flash volume formatted and mounted.");
        return;
#endif
    }

    (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                           storage_flash_log_tag,
                           "Volume unavailable; no automatic format.");
}

/**
 * @brief 给 FTL 一次有限回收机会，并记录需要显式恢复的失败。
 * @note 仅已通过 init 校验的 Storage Task 空闲周期调用。是否擦块仍由 FTL
 *       决定；本函数不改变挂载状态，也不在失败时自动恢复或格式化。
 */
void storage_flash_reclaim(void)
{
    Service_StatusTypeDef status;

    if (!storage_flash_is_storage_task(storage_flash_owner_task))
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_flash_log_tag,
                               "Storage task handle is invalid.");
        return;
    }

    status = Service_Filesystem_ReclaimFlash();
    if ((status == SERVICE_ERROR) || (status == SERVICE_TIMEOUT))
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_flash_log_tag,
                               "Reclaim failed; explicit recovery required.");
    }
}
