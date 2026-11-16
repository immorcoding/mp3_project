/**
 * @file filesystem_file_locator.c
 * @brief 按文件定位符打开文件；FatFs 类型及补丁函数不向 APP 暴露。
 */

#include "Service/filesystem/filesystem_file.h"

#include "Service/filesystem/filesystem_entry.h"
#include "Service/filesystem/filesystem_ff_patch.h"
#include "Service/filesystem/filesystem_handle.h"
#include "Service/filesystem/filesystem_lock.h"
#include "Service/filesystem/filesystem_locator.h"
#include "Service/filesystem/filesystem_path.h"
#include "Service/filesystem/filesystem_status.h"

/**
 * @brief 持卷锁时占槽并按定位符打开；只有 `f_open_dirent` 在窄表锁内。
 * @param[in] volume 目标卷，调用者已持其卷锁。
 * @param[in] drive_path 该卷根目录的 TCHAR 路径。
 * @param[in] locator 目录扫描时随条目给出的定位符。
 * @param[out] file 仅返回 OPENED 时写入有效句柄。
 * @param[out] status 写入 Service 状态。
 * @return 见 `Service_Filesystem_OpenFileByLocator`。
 */
static Service_Filesystem_LocatorOpenResultTypeDef filesystem_file_open_locator_locked(
    Service_Filesystem_VolumeTypeDef volume,
    const TCHAR *drive_path,
    const Service_Filesystem_FileLocatorTypeDef *locator,
    Service_Filesystem_FileHandleTypeDef *file,
    Service_StatusTypeDef *status)
{
    FIL *object;
    Service_Filesystem_FileHandleTypeDef handle = {0};
    FRESULT result;

    *status = filesystem_handle_acquire_file(volume, &object, &handle);
    if (*status != SERVICE_OK)
    {
        return SERVICE_FILESYSTEM_LOCATOR_FAILED;
    }

    if (!filesystem_locator_is_set(locator))
    {
        filesystem_handle_release_file(handle);
        return SERVICE_FILESYSTEM_LOCATOR_MISMATCH;
    }

    /* 卷锁 → 表锁。补丁函数像 f_open 一样登记锁表项；表锁内不拿别的锁。 */
    filesystem_lock_table_take(FILESYSTEM_OP_OPEN_LOCATOR);
    result = f_open_dirent(object,
                           drive_path,
                           locator->Opaque[FILESYSTEM_LOCATOR_WORD_FOLDER],
                           locator->Opaque[FILESYSTEM_LOCATOR_WORD_OFFSET],
                           locator->Opaque[FILESYSTEM_LOCATOR_WORD_START],
                           locator->Opaque[FILESYSTEM_LOCATOR_WORD_LENGTH]);
    filesystem_lock_table_give();
    if (result == FR_NO_FILE)
    {
        filesystem_handle_release_file(handle);
        return SERVICE_FILESYSTEM_LOCATOR_MISMATCH;
    }
    if (result != FR_OK)
    {
        filesystem_handle_release_file(handle);
        *status = filesystem_make_service_status(result);
        return SERVICE_FILESYSTEM_LOCATOR_FAILED;
    }

    *file = handle;
    return SERVICE_FILESYSTEM_LOCATOR_OPENED;
}

/**
 * @brief 按文件定位符只读打开文件，耗时与文件在文件夹内的位置无关。
 * @param[in] volume 目标卷；定位符只对生成它的卷和曲库代次有效。
 * @param[in] locator 目录扫描时随条目给出的定位符，不得为空。
 * @param[out] file 仅返回 OPENED 时写入有效句柄。
 * @param[out] status 可为空；非空时写入 Service 状态：OPENED 与 MISMATCH 为 SERVICE_OK，
 *                    FAILED 为失败原因（BUSY 为该卷槽用尽或文件被写打开，NOT_READY 为卷未就绪等）。
 * @return OPENED 已打开；MISMATCH 定位符为空或与介质上的目录项不符，未占槽，调用者应退回按路径打开；
 *         FAILED 其他失败。
 * @note  进门序列同其他入口（卷没挂立刻 NOT_READY、SD 等闸、拿所属卷的卷锁一次、拿到后再确认），失败为 `SERVICE_ERROR`
 *        且卡检测脚显示卡不在时报告 NO_MEDIA；`f_open_dirent` 一步在窄表锁内。由 `ff.c` 补丁函数读出目录项并核对“是文件、未删除、
 *        非目录、起始簇与长度一致”，再像 `f_open` 结尾那样填 FIL 并登记锁，故 `CloseFile` 照常释放锁。只读打开。
 *        约 0.35 至 0.76 ms（SD）。
 */
Service_Filesystem_LocatorOpenResultTypeDef Service_Filesystem_OpenFileByLocator(
    Service_Filesystem_VolumeTypeDef volume,
    const Service_Filesystem_FileLocatorTypeDef *locator,
    Service_Filesystem_FileHandleTypeDef *file,
    Service_StatusTypeDef *status)
{
    TCHAR drive_path[FILESYSTEM_TCHAR_PATH_LENGTH];
    Service_StatusTypeDef local_status;
    Service_Filesystem_LocatorOpenResultTypeDef result;

    filesystem_lock_check_caller();
    if (status == NULL)
    {
        status = &local_status;
    }
    *status = SERVICE_OK;

    if ((locator == NULL) || (file == NULL))
    {
        *status = SERVICE_INVALID_PARAM;
        return SERVICE_FILESYSTEM_LOCATOR_FAILED;
    }

    /* 路径转换是纯计算；卷非法时它返回 NOT_READY，所以下面的 volume 一定合法。 */
    *status = filesystem_path_make_tchar(volume, "", true, drive_path);
    if (*status != SERVICE_OK)
    {
        return SERVICE_FILESYSTEM_LOCATOR_FAILED;
    }

    *status = filesystem_entry_enter(volume, FILESYSTEM_OP_OPEN_LOCATOR, SERVICE_NOT_READY);
    if (*status != SERVICE_OK)
    {
        return SERVICE_FILESYSTEM_LOCATOR_FAILED;
    }
    result = filesystem_file_open_locator_locked(volume, drive_path, locator, file, status);
    *status = filesystem_entry_leave(volume, *status);
    return result;
}
