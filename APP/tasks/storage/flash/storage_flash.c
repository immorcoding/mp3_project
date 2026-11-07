/**
 * @file storage_flash.c
 * @brief Storage Task 的 Flash 空闲回收与失败日志。
 *
 * @details
 *          QSPI 等待与块传输仍由 Filesystem Service 私有执行器完成。Flash 的启动、挂载和格式化策略
 *          在 `APP/tasks/volume/flash/`，不在这里。
 */

#include "APP/tasks/storage/flash/storage_flash.h"
#include "APP/tasks/storage/storage_task.h"

#include "Service/filesystem/filesystem_service.h"
#include "Service/log/log_service.h"

/** @brief Storage Task 发送 Flash 子系统日志时使用的稳定标签。 */
static const char storage_flash_log_tag[] = "FLASH";

/**
 * @brief 给 FTL 一次有限回收机会，并记录需要显式恢复的失败。
 * @retval STORAGE_OK 本次回收机会已交给 FTL，或 FTL 选择不擦。
 * @retval STORAGE_ERROR 回收报告错误/超时。
 * @note 仅 Storage Task 在 Flash 卷已挂载（卷事件的挂载位为真）、没有打开的音频流时的空闲周期调用：
 *       回收要拿 Flash 卷锁，而 Flash 启动任务的 `MountFlash` 会在锁内持有 3.3 至 8.3 s，所以挂载位为真之前不能调。
 *       是否擦块仍由 FTL 决定；本函数不改变挂载状态，也不在失败时自动恢复或格式化。
 */
Storage_StatusTypeDef storage_flash_reclaim(void)
{
    Service_StatusTypeDef status = Service_Filesystem_ReclaimFlash();

    if ((status == SERVICE_ERROR) || (status == SERVICE_TIMEOUT))
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_flash_log_tag,
                               "Reclaim failed; explicit recovery required.");
        return STORAGE_ERROR;
    }

    return STORAGE_OK;
}
