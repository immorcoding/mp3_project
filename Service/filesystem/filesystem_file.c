/**
 * @file filesystem_file.c
 * @brief 卷感知文件访问；FatFs 对象及类型不向 APP 暴露。
 *
 * @details
 *          每个公开函数的结构相同：先检查调用者不在拒绝名单里（等锁之前），再做不碰共享状态的参数检查，
 *          然后在入口拿所属卷的卷锁一次，调用同名的 `_locked` 内部函数完成全部工作，最后放锁。
 *          放锁只在公开函数的这一处，内部函数怎样提前返回都不会漏放。内部函数不拿锁、不互相调用公开函数。
 */

#include "Service/filesystem/filesystem_file.h"

#include "Service/filesystem/filesystem_handle.h"
#include "Service/filesystem/filesystem_lock.h"
#include "Service/filesystem/filesystem_path.h"
#include "Service/filesystem/filesystem_status.h"

#include <stdint.h>

/**
 * @brief 将打开方式转换为 FatFs 访问标志。
 * @param[in] mode 公开打开方式。
 * @param[out] flags 接收 FatFs BYTE 标志。
 * @return true 方式合法。
 */
static bool filesystem_file_mode_flags(Service_Filesystem_FileModeTypeDef mode, BYTE *flags)
{
    if (flags == NULL)
    {
        return false;
    }

    switch (mode)
    {
        case SERVICE_FILESYSTEM_FILE_MODE_READ:
            *flags = FA_READ;
            return true;
        case SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW:
            *flags = (BYTE)(FA_CREATE_NEW | FA_READ | FA_WRITE);
            return true;
        case SERVICE_FILESYSTEM_FILE_MODE_READ_WRITE:
            *flags = (BYTE)(FA_OPEN_EXISTING | FA_READ | FA_WRITE);
            return true;
        case SERVICE_FILESYSTEM_FILE_MODE_APPEND:
            *flags = (BYTE)(FA_OPEN_EXISTING | FA_READ | FA_WRITE);
            return true;
        default:
            return false;
    }
}

/**
 * @brief 持卷锁时占槽并打开文件；只有 `f_open` 在窄表锁内。
 * @param[in] volume 目标卷，调用者已持其卷锁。
 * @param[in] native_path 已转换好的 TCHAR 路径。
 * @param[in] mode 打开方式。
 * @param[in] flags 与 mode 对应的 FatFs 标志。
 * @param[out] file 仅成功时写入有效句柄。
 * @return 见 `Service_Filesystem_OpenFile`。
 */
static Service_StatusTypeDef filesystem_file_open_locked(Service_Filesystem_VolumeTypeDef volume,
                                                         const TCHAR *native_path,
                                                         Service_Filesystem_FileModeTypeDef mode,
                                                         BYTE flags,
                                                         Service_Filesystem_FileHandleTypeDef *file)
{
    FIL *object;
    Service_Filesystem_FileHandleTypeDef handle = {0};
    FRESULT result;
    Service_StatusTypeDef status = filesystem_handle_acquire_file(volume, &object, &handle);

    if (status != SERVICE_OK)
    {
        return status;
    }

    /* 卷锁 → 表锁。f_open 的“找空位并占用”会写跨卷共用的 Files[]；表锁内不拿别的锁。 */
    filesystem_lock_table_take(FILESYSTEM_OP_OPEN);
    result = f_open(object, native_path, flags);
    filesystem_lock_table_give();
    if (result != FR_OK)
    {
        filesystem_handle_release_file(handle);
        return filesystem_make_service_status(result);
    }

    if (mode == SERVICE_FILESYSTEM_FILE_MODE_APPEND)
    {
        result = f_lseek(object, f_size(object));
        if (result != FR_OK)
        {
            (void)f_close(object);
            filesystem_handle_release_file(handle);
            return filesystem_make_service_status(result);
        }
    }

    *file = handle;
    return SERVICE_OK;
}

/**
 * @brief 在已挂载卷上打开文件，由 Service 持有实际 FIL。
 * @param[in] volume 目标卷。
 * @param[in] path UTF-8 相对路径，不能为空或表示根目录。
 * @param[in] mode 打开方式。
 * @param[out] file 仅成功时写入有效句柄。
 * @return OK 为打开成功；BUSY 为该卷的槽用尽或同名冲突；其他状态见路径与 FatFs 映射。
 * @note  拿所属卷的卷锁一次；`f_open` 一步在窄表锁内。CREATE_NEW 同时允许读回以便 Seek 校验。
 *        APPEND 在打开已有文件后定位到末尾。不自动挂载或格式化。
 */
Service_StatusTypeDef Service_Filesystem_OpenFile(Service_Filesystem_VolumeTypeDef volume,
                                                  const char *path,
                                                  Service_Filesystem_FileModeTypeDef mode,
                                                  Service_Filesystem_FileHandleTypeDef *file)
{
    TCHAR native_path[FILESYSTEM_TCHAR_PATH_LENGTH];
    BYTE flags;
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if ((file == NULL) || !filesystem_file_mode_flags(mode, &flags))
    {
        return SERVICE_INVALID_PARAM;
    }

    /* 路径转换是纯计算；卷非法时它返回 NOT_READY，所以下面的 volume 一定合法。 */
    status = filesystem_path_make_tchar(volume, path, false, native_path);
    if (status != SERVICE_OK)
    {
        return status;
    }

    filesystem_lock_take(volume, FILESYSTEM_OP_OPEN);
    status = filesystem_file_open_locked(volume, native_path, mode, flags, file);
    filesystem_lock_give(volume);
    return status;
}

/**
 * @brief 同步读取当前文件并报告实际字节数。
 * @param[in] file 当前有效句柄。
 * @param[out] buffer 至少 requested 字节的有效缓冲。
 * @param[in] requested 非零字节数，不超过 `SERVICE_FILESYSTEM_IO_MAX_BYTES`（32 KiB）；超过返回 INVALID_PARAM，
 *                      不截短、不拆分。
 * @param[out] transferred 实际读取字节数；校验失败时为零。
 * @return OK 只表示本次读取无错误，短读可能为 EOF。
 * @note  拿所属卷的卷锁一次，句柄有效性在锁内检查。
 */
Service_StatusTypeDef Service_Filesystem_ReadFile(Service_Filesystem_FileHandleTypeDef file,
                                                  void *buffer,
                                                  uint32_t requested,
                                                  uint32_t *transferred)
{
    Service_Filesystem_VolumeTypeDef volume;
    FIL *object;
    UINT count = 0U;
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (transferred == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    *transferred = 0U;
    if ((buffer == NULL) || (requested == 0U) || (requested > SERVICE_FILESYSTEM_IO_MAX_BYTES))
    {
        return SERVICE_INVALID_PARAM;
    }
    if (!filesystem_handle_file_volume(file, &volume))
    {
        return SERVICE_INVALID_HANDLE;
    }

    filesystem_lock_take(volume, FILESYSTEM_OP_READ);
    status = filesystem_handle_lookup_file(file, &object);
    if (status == SERVICE_OK)
    {
        FRESULT result = f_read(object, buffer, (UINT)requested, &count);

        *transferred = count;
        status = filesystem_make_service_status(result);
    }
    filesystem_lock_give(volume);
    return status;
}

/**
 * @brief 同步写入当前文件，保留部分传输量语义。
 * @param[in] file 以可写方式打开的有效句柄。
 * @param[in] buffer 至少 requested 字节的输入。
 * @param[in] requested 非零字节数，不超过 `SERVICE_FILESYSTEM_IO_MAX_BYTES`（32 KiB）；超过返回 INVALID_PARAM。
 * @param[out] transferred 实际写入字节数；OK 时短写可能表示空间不足。
 * @return OK 表示 FatFs 未报告错误，不代表已经持久化。
 * @note  拿所属卷的卷锁一次；需要掉电边界时调用 SyncFile。Flash 上写 32 KiB 约持锁 1.5 s（推算，未测），
 *        只能由后台任务调用。
 */
Service_StatusTypeDef Service_Filesystem_WriteFile(Service_Filesystem_FileHandleTypeDef file,
                                                   const void *buffer,
                                                   uint32_t requested,
                                                   uint32_t *transferred)
{
    Service_Filesystem_VolumeTypeDef volume;
    FIL *object;
    UINT count = 0U;
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (transferred == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    *transferred = 0U;
    if ((buffer == NULL) || (requested == 0U) || (requested > SERVICE_FILESYSTEM_IO_MAX_BYTES))
    {
        return SERVICE_INVALID_PARAM;
    }
    if (!filesystem_handle_file_volume(file, &volume))
    {
        return SERVICE_INVALID_HANDLE;
    }

    filesystem_lock_take(volume, FILESYSTEM_OP_WRITE);
    status = filesystem_handle_lookup_file(file, &object);
    if (status == SERVICE_OK)
    {
        FRESULT result = f_write(object, buffer, (UINT)requested, &count);

        *transferred = count;
        status = filesystem_make_service_status(result);
    }
    filesystem_lock_give(volume);
    return status;
}

/**
 * @brief 将文件读写指针移动到指定偏移。
 * @param[in] file 当前有效句柄。
 * @param[in] offset 从文件起始计算的字节偏移，不得超过 32 位 FatFs FSIZE。
 * @return OK 为定位成功。
 * @note  拿所属卷的卷锁一次。当前未启用 exFAT，偏移超过 4 GiB-1 视为参数错误。
 */
Service_StatusTypeDef Service_Filesystem_SeekFile(Service_Filesystem_FileHandleTypeDef file,
                                                  uint64_t offset)
{
    Service_Filesystem_VolumeTypeDef volume;
    FIL *object;
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (!filesystem_handle_file_volume(file, &volume))
    {
        return SERVICE_INVALID_HANDLE;
    }

    filesystem_lock_take(volume, FILESYSTEM_OP_SEEK);
    status = filesystem_handle_lookup_file(file, &object);
    if (status == SERVICE_OK)
    {
        status = (offset > (uint64_t)UINT32_MAX)
                     ? SERVICE_INVALID_PARAM
                     : filesystem_make_service_status(f_lseek(object, (FSIZE_t)offset));
    }
    filesystem_lock_give(volume);
    return status;
}

/**
 * @brief 读取当前文件指针位置。
 * @param[in] file 当前有效句柄。
 * @param[out] position 接收从文件起始计算的字节偏移。
 * @return OK 为查询成功。
 * @note  拿所属卷的卷锁一次：FIL 里的位置由读写的持锁者改，不加锁读会读到一半。
 */
Service_StatusTypeDef Service_Filesystem_GetFilePosition(Service_Filesystem_FileHandleTypeDef file,
                                                         uint64_t *position)
{
    Service_Filesystem_VolumeTypeDef volume;
    FIL *object;
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (position == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    *position = 0U;
    if (!filesystem_handle_file_volume(file, &volume))
    {
        return SERVICE_INVALID_HANDLE;
    }

    filesystem_lock_take(volume, FILESYSTEM_OP_TELL);
    status = filesystem_handle_lookup_file(file, &object);
    if (status == SERVICE_OK)
    {
        *position = (uint64_t)f_tell(object);
    }
    filesystem_lock_give(volume);
    return status;
}

/**
 * @brief 读取当前打开文件的大小。
 * @param[in] file 当前有效句柄。
 * @param[out] size 接收字节大小。
 * @return OK 为查询成功。
 * @note  拿所属卷的卷锁一次。
 */
Service_StatusTypeDef Service_Filesystem_GetFileSize(Service_Filesystem_FileHandleTypeDef file,
                                                     uint64_t *size)
{
    Service_Filesystem_VolumeTypeDef volume;
    FIL *object;
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (size == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    *size = 0U;
    if (!filesystem_handle_file_volume(file, &volume))
    {
        return SERVICE_INVALID_HANDLE;
    }

    filesystem_lock_take(volume, FILESYSTEM_OP_SIZE);
    status = filesystem_handle_lookup_file(file, &object);
    if (status == SERVICE_OK)
    {
        *size = (uint64_t)f_size(object);
    }
    filesystem_lock_give(volume);
    return status;
}

/**
 * @brief 同步文件缓存、目录元数据及底层逻辑盘。
 * @param[in] file 当前有效句柄。
 * @return OK 表示 f_sync 完成，写入语义已提交到 FTL/SD。
 * @note  拿所属卷的卷锁一次；不能将跨组 FatFs 更新视为掉电原子事务。
 */
Service_StatusTypeDef Service_Filesystem_SyncFile(Service_Filesystem_FileHandleTypeDef file)
{
    Service_Filesystem_VolumeTypeDef volume;
    FIL *object;
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (!filesystem_handle_file_volume(file, &volume))
    {
        return SERVICE_INVALID_HANDLE;
    }

    filesystem_lock_take(volume, FILESYSTEM_OP_SYNC);
    status = filesystem_handle_lookup_file(file, &object);
    if (status == SERVICE_OK)
    {
        status = filesystem_make_service_status(f_sync(object));
    }
    filesystem_lock_give(volume);
    return status;
}

/**
 * @brief 关闭文件；FatFs 关闭路径会刷新尚未同步的缓冲。
 * @param[in] file 当前有效句柄。关闭失败时槽位保留，可用同一 Token 重试。
 * @return OK 表示已关闭；句柄失效返回 INVALID_HANDLE。
 * @note  拿所属卷的卷锁一次，关闭文件与释放槽在同一次持锁里。成功后调用者持有的 Token 立即失效，无需由本函数清零。
 *        `f_close` 不在表锁内（它只写本卷的锁表项）。
 */
Service_StatusTypeDef Service_Filesystem_CloseFile(Service_Filesystem_FileHandleTypeDef file)
{
    Service_Filesystem_VolumeTypeDef volume;
    FIL *object;
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (!filesystem_handle_file_volume(file, &volume))
    {
        return SERVICE_INVALID_HANDLE;
    }

    filesystem_lock_take(volume, FILESYSTEM_OP_CLOSE);
    status = filesystem_handle_lookup_file(file, &object);
    if (status == SERVICE_OK)
    {
        status = filesystem_make_service_status(f_close(object));
        if (status == SERVICE_OK)
        {
            filesystem_handle_release_file(file);
        }
    }
    filesystem_lock_give(volume);
    return status;
}

/**
 * @brief 持卷锁时删除文件。
 * @param[in] volume 目标卷，调用者已持其卷锁。
 * @param[in] path UTF-8 相对路径。
 * @return 见 `Service_Filesystem_RemoveFile`。
 */
static Service_StatusTypeDef filesystem_file_remove_locked(Service_Filesystem_VolumeTypeDef volume,
                                                           const char *path)
{
    TCHAR native_path[FILESYSTEM_TCHAR_PATH_LENGTH];
    FILINFO info;
    Service_StatusTypeDef status = filesystem_handle_volume_ready(volume);

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = filesystem_path_make_tchar(volume, path, false, native_path);
    if (status != SERVICE_OK)
    {
        return status;
    }

    FRESULT result = f_stat(native_path, &info);
    if (result == FR_NO_FILE)
    {
        return SERVICE_OK;
    }
    if (result != FR_OK)
    {
        return filesystem_make_service_status(result);
    }
    if ((info.fattrib & AM_DIR) != 0U)
    {
        return SERVICE_INVALID_PARAM;
    }

    return filesystem_make_service_status(f_unlink(native_path));
}

/**
 * @brief 删除所选卷上的文件并同步文件系统元数据，不执行物理安全擦除。
 * @param[in] volume 目标卷。
 * @param[in] path UTF-8 相对路径。
 * @return OK 为已删除或文件已不存在；目录目标返回 INVALID_PARAM。
 * @note  拿所属卷的卷锁一次，`f_stat` 与 `f_unlink` 之间不放。无 TRIM 时只释放 FAT 簇，后续重写 LBA 才使旧 FTL 版本失效。
 *        Flash 上删文件约 138 ms/MiB，只能由后台任务调用；SD 上的大文件未测。
 */
Service_StatusTypeDef Service_Filesystem_RemoveFile(Service_Filesystem_VolumeTypeDef volume,
                                                    const char *path)
{
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (!filesystem_lock_volume_valid(volume))
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_lock_take(volume, FILESYSTEM_OP_REMOVE);
    status = filesystem_file_remove_locked(volume, path);
    filesystem_lock_give(volume);
    return status;
}

/**
 * @brief 持卷锁时重命名。
 * @param[in] volume 目标卷，调用者已持其卷锁。
 * @param[in] old_path 现有 UTF-8 相对路径。
 * @param[in] new_path 新的 UTF-8 相对路径。
 * @return 见 `Service_Filesystem_RenameFile`。
 */
static Service_StatusTypeDef filesystem_file_rename_locked(Service_Filesystem_VolumeTypeDef volume,
                                                           const char *old_path,
                                                           const char *new_path)
{
    TCHAR native_old[FILESYSTEM_TCHAR_PATH_LENGTH];
    TCHAR native_new[FILESYSTEM_TCHAR_PATH_LENGTH];
    Service_StatusTypeDef status = filesystem_handle_volume_ready(volume);

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = filesystem_path_make_tchar(volume, old_path, false, native_old);
    if (status != SERVICE_OK)
    {
        return status;
    }
    status = filesystem_path_make_tchar(volume, new_path, false, native_new);
    if (status != SERVICE_OK)
    {
        return status;
    }

    return filesystem_make_service_status(f_rename(native_old, native_new));
}

/**
 * @brief 在同一卷内重命名或移动文件/空目录项。
 * @param[in] volume 目标卷。
 * @param[in] old_path 现有 UTF-8 相对路径。
 * @param[in] new_path 新的 UTF-8 相对路径。
 * @return OK 为重命名成功；两条路径都经过相同的路径契约校验。
 * @note  拿所属卷的卷锁一次。Flash 上约 66 至 81 ms。
 */
Service_StatusTypeDef Service_Filesystem_RenameFile(Service_Filesystem_VolumeTypeDef volume,
                                                    const char *old_path,
                                                    const char *new_path)
{
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (!filesystem_lock_volume_valid(volume))
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_lock_take(volume, FILESYSTEM_OP_RENAME);
    status = filesystem_file_rename_locked(volume, old_path, new_path);
    filesystem_lock_give(volume);
    return status;
}

/**
 * @brief 持卷锁时查询元数据。
 * @param[in] volume 目标卷，调用者已持其卷锁。
 * @param[in] path UTF-8 相对路径。
 * @param[out] info 仅成功时写入。
 * @return 见 `Service_Filesystem_GetFileInfo`。
 */
static Service_StatusTypeDef filesystem_file_info_locked(Service_Filesystem_VolumeTypeDef volume,
                                                         const char *path,
                                                         Service_Filesystem_FileInfoTypeDef *info)
{
    TCHAR native_path[FILESYSTEM_TCHAR_PATH_LENGTH];
    FILINFO native_info;
    Service_StatusTypeDef status = filesystem_handle_volume_ready(volume);

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = filesystem_path_make_tchar(volume, path, false, native_path);
    if (status != SERVICE_OK)
    {
        return status;
    }

    FRESULT result = f_stat(native_path, &native_info);
    if (result != FR_OK)
    {
        return filesystem_make_service_status(result);
    }

    info->Size = (uint64_t)native_info.fsize;
    info->Date = native_info.fdate;
    info->Time = native_info.ftime;
    info->IsDirectory = ((native_info.fattrib & AM_DIR) != 0U);
    info->IsReadOnly = ((native_info.fattrib & AM_RDO) != 0U);
    return SERVICE_OK;
}

/**
 * @brief 查询所选卷上文件或目录的元数据。
 * @param[in] volume 目标卷。
 * @param[in] path UTF-8 相对路径。
 * @param[out] info 仅成功时写入。
 * @return OK 为查询成功；目标不存在映射为底层错误状态。
 * @note  拿所属卷的卷锁一次。
 */
Service_StatusTypeDef Service_Filesystem_GetFileInfo(Service_Filesystem_VolumeTypeDef volume,
                                                     const char *path,
                                                     Service_Filesystem_FileInfoTypeDef *info)
{
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (info == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    if (!filesystem_lock_volume_valid(volume))
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_lock_take(volume, FILESYSTEM_OP_INFO);
    status = filesystem_file_info_locked(volume, path, info);
    filesystem_lock_give(volume);
    return status;
}
