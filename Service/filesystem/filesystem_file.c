/**
 * @file filesystem_file.c
 * @brief 卷感知文件访问；FatFs 对象及类型不向 APP 暴露。
 *
 * @details
 *          每个公开函数入口拿一次卷锁、返回前放（spike #191）。按卷传参的函数直接拿该卷的锁；
 *          按 Token 操作的函数先取出 Token 所属的卷，拿到卷锁后再次校验 Token。会碰 FatFs 全局锁表
 *          `Files[]` 的调用（`f_open`、`f_close`、`f_unlink`、`f_rename`）在每卷一把锁时包在打开表锁内；
 *          写打开的文件在表锁外先 `f_sync`，不让 Flash 写盘持有表锁。
 */

#include "Service/filesystem/filesystem_file.h"

#include "Service/filesystem/filesystem_handle.h"
#include "Service/filesystem/filesystem_lock.h"
#include "Service/filesystem/filesystem_path.h"
#include "Service/filesystem/filesystem_status.h"

#include <limits.h>
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
 * @brief 在已挂载卷上打开文件的不加锁实现。
 * @param[in] volume 目标卷；调用者持有它的卷锁。
 * @param[in] path UTF-8 相对路径，不能为空或表示根目录。
 * @param[in] mode 打开方式。
 * @param[in] flags 与 mode 对应的 FatFs 访问标志。
 * @param[out] file 仅成功时写入有效句柄。
 * @return 同 `Service_Filesystem_OpenFile`。
 */
static Service_StatusTypeDef filesystem_file_open_locked(Service_Filesystem_VolumeTypeDef volume,
                                                         const char *path,
                                                         Service_Filesystem_FileModeTypeDef mode,
                                                         BYTE flags,
                                                         Service_Filesystem_FileHandleTypeDef *file)
{
    TCHAR native_path[FILESYSTEM_TCHAR_PATH_LENGTH];
    FIL *object;
    Service_Filesystem_FileHandleTypeDef handle = {0};
    Service_StatusTypeDef status;
    FRESULT result;

    status = filesystem_path_make_tchar(volume, path, false, native_path);
    if (status != SERVICE_OK)
    {
        return status;
    }

    status = filesystem_handle_acquire_file(volume, &object, &handle);
    if (status != SERVICE_OK)
    {
        return status;
    }

    filesystem_lock_table_acquire("OpenFile");
    result = f_open(object, native_path, flags);
    filesystem_lock_table_release();
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
            filesystem_lock_table_wide_acquire("OpenFile");
            (void)f_close(object);
            filesystem_lock_table_wide_release();
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
 * @return OK 为打开成功；BUSY 为槽用尽或同名冲突；其他状态见路径与 FatFs 映射。
 * @note 入口拿卷锁。CREATE_NEW 同时允许读回以便 Seek 校验。
 *       APPEND 在打开已有文件后定位到末尾。不自动挂载或格式化。
 */
Service_StatusTypeDef Service_Filesystem_OpenFile(Service_Filesystem_VolumeTypeDef volume,
                                                  const char *path,
                                                  Service_Filesystem_FileModeTypeDef mode,
                                                  Service_Filesystem_FileHandleTypeDef *file)
{
    BYTE flags;
    Service_StatusTypeDef status;

    if ((file == NULL) || !filesystem_file_mode_flags(mode, &flags) ||
        !filesystem_handle_volume_is_valid(volume))
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_lock_acquire(volume, "OpenFile");
    status = filesystem_file_open_locked(volume, path, mode, flags, file);
    filesystem_lock_release(volume);
    return status;
}

/**
 * @brief 同步读取当前文件并报告实际字节数。
 * @param[in] file 当前有效句柄。
 * @param[out] buffer 至少 requested 字节的有效缓冲。
 * @param[in] requested 非零字节数，不超过 FatFs UINT 可表示范围。
 * @param[out] transferred 实际读取字节数；校验失败时为零。
 * @return OK 只表示本次读取无错误，短读可能为 EOF。
 * @note 入口拿 Token 所属卷的卷锁，返回前放。
 */
Service_StatusTypeDef Service_Filesystem_ReadFile(Service_Filesystem_FileHandleTypeDef file,
                                                  void *buffer,
                                                  uint32_t requested,
                                                  uint32_t *transferred)
{
    FIL *object;
    Service_Filesystem_VolumeTypeDef volume;
    UINT count = 0U;
    Service_StatusTypeDef status;
    FRESULT result;

    if (transferred == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    *transferred = 0U;
    if ((buffer == NULL) || (requested == 0U) || (requested > (uint32_t)UINT_MAX))
    {
        return SERVICE_INVALID_PARAM;
    }

    status = filesystem_handle_lock_file(file, "ReadFile", &object, &volume);
    if (status != SERVICE_OK)
    {
        return status;
    }

    result = f_read(object, buffer, (UINT)requested, &count);
    filesystem_lock_release(volume);
    *transferred = count;
    return filesystem_make_service_status(result);
}

/**
 * @brief 同步写入当前文件，保留部分传输量语义。
 * @param[in] file 以可写方式打开的有效句柄。
 * @param[in] buffer 至少 requested 字节的输入。
 * @param[in] requested 非零字节数，不超过 FatFs UINT 可表示范围。
 * @param[out] transferred 实际写入字节数；OK 时短写可能表示空间不足。
 * @return OK 表示 FatFs 未报告错误，不代表已经持久化。
 * @note 入口拿 Token 所属卷的卷锁；需要掉电边界时调用 SyncFile。
 */
Service_StatusTypeDef Service_Filesystem_WriteFile(Service_Filesystem_FileHandleTypeDef file,
                                                   const void *buffer,
                                                   uint32_t requested,
                                                   uint32_t *transferred)
{
    FIL *object;
    Service_Filesystem_VolumeTypeDef volume;
    UINT count = 0U;
    Service_StatusTypeDef status;
    FRESULT result;

    if (transferred == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    *transferred = 0U;
    if ((buffer == NULL) || (requested == 0U) || (requested > (uint32_t)UINT_MAX))
    {
        return SERVICE_INVALID_PARAM;
    }

    status = filesystem_handle_lock_file(file, "WriteFile", &object, &volume);
    if (status != SERVICE_OK)
    {
        return status;
    }

    result = f_write(object, buffer, (UINT)requested, &count);
    filesystem_lock_release(volume);
    *transferred = count;
    return filesystem_make_service_status(result);
}

/**
 * @brief 将文件读写指针移动到指定偏移。
 * @param[in] file 当前有效句柄。
 * @param[in] offset 从文件起始计算的字节偏移，不得超过 32 位 FatFs FSIZE。
 * @return OK 为定位成功。
 * @note 入口拿 Token 所属卷的卷锁。当前未启用 exFAT，偏移超过 4 GiB-1 视为参数错误。
 */
Service_StatusTypeDef Service_Filesystem_SeekFile(Service_Filesystem_FileHandleTypeDef file,
                                                  uint64_t offset)
{
    FIL *object;
    Service_Filesystem_VolumeTypeDef volume;
    Service_StatusTypeDef status = filesystem_handle_lock_file(file, "SeekFile", &object, &volume);
    FRESULT result;

    if (status != SERVICE_OK)
    {
        return status;
    }
    if (offset > (uint64_t)UINT32_MAX)
    {
        filesystem_lock_release(volume);
        return SERVICE_INVALID_PARAM;
    }

    result = f_lseek(object, (FSIZE_t)offset);
    filesystem_lock_release(volume);
    return filesystem_make_service_status(result);
}

/**
 * @brief 读取当前文件指针位置。
 * @param[in] file 当前有效句柄。
 * @param[out] position 接收从文件起始计算的字节偏移。
 * @return OK 为查询成功。
 * @note 入口拿 Token 所属卷的卷锁。
 */
Service_StatusTypeDef Service_Filesystem_GetFilePosition(Service_Filesystem_FileHandleTypeDef file,
                                                         uint64_t *position)
{
    FIL *object;
    Service_Filesystem_VolumeTypeDef volume;
    Service_StatusTypeDef status;

    if (position == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    *position = 0U;
    status = filesystem_handle_lock_file(file, "FilePosition", &object, &volume);
    if (status != SERVICE_OK)
    {
        return status;
    }

    *position = (uint64_t)f_tell(object);
    filesystem_lock_release(volume);
    return SERVICE_OK;
}

/**
 * @brief 读取当前打开文件的大小。
 * @param[in] file 当前有效句柄。
 * @param[out] size 接收字节大小。
 * @return OK 为查询成功。
 * @note 入口拿 Token 所属卷的卷锁。
 */
Service_StatusTypeDef Service_Filesystem_GetFileSize(Service_Filesystem_FileHandleTypeDef file,
                                                     uint64_t *size)
{
    FIL *object;
    Service_Filesystem_VolumeTypeDef volume;
    Service_StatusTypeDef status;

    if (size == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    *size = 0U;
    status = filesystem_handle_lock_file(file, "FileSize", &object, &volume);
    if (status != SERVICE_OK)
    {
        return status;
    }

    *size = (uint64_t)f_size(object);
    filesystem_lock_release(volume);
    return SERVICE_OK;
}

/**
 * @brief 同步文件缓存、目录元数据及底层逻辑盘。
 * @param[in] file 当前有效句柄。
 * @return OK 表示 f_sync 完成，写入语义已提交到 FTL/SD。
 * @note 入口拿 Token 所属卷的卷锁并在锁内同步等待；不能将跨组 FatFs 更新视为掉电原子事务。
 */
Service_StatusTypeDef Service_Filesystem_SyncFile(Service_Filesystem_FileHandleTypeDef file)
{
    FIL *object;
    Service_Filesystem_VolumeTypeDef volume;
    Service_StatusTypeDef status = filesystem_handle_lock_file(file, "SyncFile", &object, &volume);
    FRESULT result;

    if (status != SERVICE_OK)
    {
        return status;
    }

    result = f_sync(object);
    filesystem_lock_release(volume);
    return filesystem_make_service_status(result);
}

/**
 * @brief 关闭文件；写打开的文件先在表锁外同步，再在打开表锁内 `f_close`。
 * @param[in] file 当前有效句柄。关闭失败时槽位保留，可用同一 Token 重试。
 * @return OK 表示已关闭；句柄失效返回 INVALID_HANDLE。
 * @note 入口拿 Token 所属卷的卷锁。成功后调用者持有的 Token 立即失效，无需由本函数清零。
 *       `f_sync` 之后文件不再有脏数据，`f_close` 内部的同步不会再落盘，所以 Flash 写不会持有表锁。
 */
Service_StatusTypeDef Service_Filesystem_CloseFile(Service_Filesystem_FileHandleTypeDef file)
{
    FIL *object;
    Service_Filesystem_VolumeTypeDef volume;
    Service_StatusTypeDef status = filesystem_handle_lock_file(file, "CloseFile", &object, &volume);
    FRESULT result = FR_OK;

    if (status != SERVICE_OK)
    {
        return status;
    }

    if ((object->flag & FA_WRITE) != 0U)
    {
        result = f_sync(object);
    }
    if (result == FR_OK)
    {
        filesystem_lock_table_wide_acquire("CloseFile");
        result = f_close(object);
        filesystem_lock_table_wide_release();
    }

    status = filesystem_make_service_status(result);
    if (status == SERVICE_OK)
    {
        filesystem_handle_release_file(file);
    }
    filesystem_lock_release(volume);
    return status;
}

/**
 * @brief 删除所选卷上的文件的不加锁实现。
 * @param[in] volume 目标卷；调用者持有它的卷锁。
 * @param[in] path UTF-8 相对路径。
 * @return 同 `Service_Filesystem_RemoveFile`。
 */
static Service_StatusTypeDef filesystem_file_remove_locked(Service_Filesystem_VolumeTypeDef volume,
                                                           const char *path)
{
    TCHAR native_path[FILESYSTEM_TCHAR_PATH_LENGTH];
    FILINFO info;
    FRESULT result;
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

    result = f_stat(native_path, &info);
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

    filesystem_lock_table_wide_acquire("RemoveFile");
    result = f_unlink(native_path);
    filesystem_lock_table_wide_release();
    return filesystem_make_service_status(result);
}

/**
 * @brief 删除所选卷上的文件并同步文件系统元数据，不执行物理安全擦除。
 * @param[in] volume 目标卷。
 * @param[in] path UTF-8 相对路径。
 * @return OK 为已删除或文件已不存在；目录目标返回 INVALID_PARAM。
 * @note 入口拿卷锁；`f_unlink` 在打开表锁内。无 TRIM 时只释放 FAT 簇，后续重写 LBA 才使旧 FTL 版本失效。
 */
Service_StatusTypeDef Service_Filesystem_RemoveFile(Service_Filesystem_VolumeTypeDef volume,
                                                    const char *path)
{
    Service_StatusTypeDef status;

    if (!filesystem_handle_volume_is_valid(volume))
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_lock_acquire(volume, "RemoveFile");
    status = filesystem_file_remove_locked(volume, path);
    filesystem_lock_release(volume);
    return status;
}

/**
 * @brief 在同一卷内重命名或移动文件/空目录项的不加锁实现。
 * @param[in] volume 目标卷；调用者持有它的卷锁。
 * @param[in] old_path 现有 UTF-8 相对路径。
 * @param[in] new_path 新的 UTF-8 相对路径。
 * @return 同 `Service_Filesystem_RenameFile`。
 */
static Service_StatusTypeDef filesystem_file_rename_locked(Service_Filesystem_VolumeTypeDef volume,
                                                           const char *old_path,
                                                           const char *new_path)
{
    TCHAR native_old[FILESYSTEM_TCHAR_PATH_LENGTH];
    TCHAR native_new[FILESYSTEM_TCHAR_PATH_LENGTH];
    FRESULT result;
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

    filesystem_lock_table_wide_acquire("RenameFile");
    result = f_rename(native_old, native_new);
    filesystem_lock_table_wide_release();
    return filesystem_make_service_status(result);
}

/**
 * @brief 在同一卷内重命名或移动文件/空目录项。
 * @param[in] volume 目标卷。
 * @param[in] old_path 现有 UTF-8 相对路径。
 * @param[in] new_path 新的 UTF-8 相对路径。
 * @return OK 为重命名成功；两条路径都经过相同的路径契约校验。
 * @note 入口拿卷锁；`f_rename` 在打开表锁内。
 */
Service_StatusTypeDef Service_Filesystem_RenameFile(Service_Filesystem_VolumeTypeDef volume,
                                                    const char *old_path,
                                                    const char *new_path)
{
    Service_StatusTypeDef status;

    if (!filesystem_handle_volume_is_valid(volume))
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_lock_acquire(volume, "RenameFile");
    status = filesystem_file_rename_locked(volume, old_path, new_path);
    filesystem_lock_release(volume);
    return status;
}

/**
 * @brief 查询所选卷上文件或目录元数据的不加锁实现。
 * @param[in] volume 目标卷；调用者持有它的卷锁。
 * @param[in] path UTF-8 相对路径。
 * @param[out] info 仅成功时写入。
 * @return 同 `Service_Filesystem_GetFileInfo`。
 */
static Service_StatusTypeDef filesystem_file_info_locked(Service_Filesystem_VolumeTypeDef volume,
                                                         const char *path,
                                                         Service_Filesystem_FileInfoTypeDef *info)
{
    TCHAR native_path[FILESYSTEM_TCHAR_PATH_LENGTH];
    FILINFO native_info;
    FRESULT result;
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

    result = f_stat(native_path, &native_info);
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
 * @note 入口拿卷锁。
 */
Service_StatusTypeDef Service_Filesystem_GetFileInfo(Service_Filesystem_VolumeTypeDef volume,
                                                     const char *path,
                                                     Service_Filesystem_FileInfoTypeDef *info)
{
    Service_StatusTypeDef status;

    if (info == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    if (!filesystem_handle_volume_is_valid(volume))
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_lock_acquire(volume, "GetFileInfo");
    status = filesystem_file_info_locked(volume, path, info);
    filesystem_lock_release(volume);
    return status;
}
