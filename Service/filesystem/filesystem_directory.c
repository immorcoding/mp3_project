/**
 * @file filesystem_directory.c
 * @brief 卷感知目录访问；FatFs DIR 对象不向 APP 暴露。
 */

#include "Service/filesystem/filesystem_directory.h"

#include "Service/filesystem/filesystem_handle.h"
#include "Service/filesystem/filesystem_path.h"
#include "Service/filesystem/filesystem_status.h"

#include <string.h>

/**
 * @brief 打开所选卷上的目录；空路径表示该卷根目录。
 * @param[in] volume 目标卷。
 * @param[in] path UTF-8 相对路径，空字符串表示根目录。
 * @param[out] directory 仅成功时写入有效句柄。
 * @return OK 为打开成功；BUSY 为目录槽用尽。
 * @note 仅 Storage Task 调用。
 */
Service_StatusTypeDef Service_Filesystem_OpenDirectory(
    Service_Filesystem_VolumeTypeDef volume,
    const char *path,
    Service_Filesystem_DirectoryHandleTypeDef *directory)
{
    TCHAR native_path[FILESYSTEM_TCHAR_PATH_LENGTH];
    DIR *object;
    Service_Filesystem_DirectoryHandleTypeDef handle = {0};
    Service_StatusTypeDef status;

    if (directory == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }

    status = filesystem_path_make_tchar(volume, path, true, native_path);
    if (status != SERVICE_OK)
    {
        return status;
    }

    status = filesystem_handle_acquire_directory(volume, &object, &handle);
    if (status != SERVICE_OK)
    {
        return status;
    }

    FRESULT result = f_opendir(object, native_path);
    if (result != FR_OK)
    {
        filesystem_handle_release_directory(handle);
        return filesystem_make_service_status(result);
    }

    *directory = handle;
    return SERVICE_OK;
}

/**
 * @brief 读取下一个目录项。
 * @param[in] directory 当前有效句柄。
 * @param[out] entry 接收 UTF-8 名称与属性；读到结尾时 Name 为空且仍返回 OK。
 * @return OK 为本次读取无错误。
 * @note 仅 Storage Task 调用。跳过 FatFs 返回的空项之外的转换失败视为 ERROR。
 */
Service_StatusTypeDef Service_Filesystem_ReadDirectory(
    Service_Filesystem_DirectoryHandleTypeDef directory,
    Service_Filesystem_DirectoryEntryTypeDef *entry)
{
    DIR *object;
    FILINFO info;
    Service_StatusTypeDef status;

    if (entry == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    memset(entry, 0, sizeof(*entry));
    status = filesystem_handle_lookup_directory(directory, &object);
    if (status != SERVICE_OK)
    {
        return status;
    }

    FRESULT result = f_readdir(object, &info);
    if (result != FR_OK)
    {
        return filesystem_make_service_status(result);
    }
    if (info.fname[0] == 0)
    {
        return SERVICE_OK;
    }
    if (!filesystem_path_tchar_name_to_utf8(info.fname, entry->Name))
    {
        return SERVICE_ERROR;
    }

    entry->Size = (uint64_t)info.fsize;
    entry->Date = info.fdate;
    entry->Time = info.ftime;
    entry->IsDirectory = ((info.fattrib & AM_DIR) != 0U);
    entry->IsReadOnly = ((info.fattrib & AM_RDO) != 0U);
    return SERVICE_OK;
}

/**
 * @brief 将目录读取位置回到开头。
 * @param[in] directory 当前有效句柄。
 * @return OK 为复位成功。
 */
Service_StatusTypeDef Service_Filesystem_RewindDirectory(
    Service_Filesystem_DirectoryHandleTypeDef directory)
{
    DIR *object;
    Service_StatusTypeDef status = filesystem_handle_lookup_directory(directory, &object);

    if (status != SERVICE_OK)
    {
        return status;
    }

    return filesystem_make_service_status(f_readdir(object, NULL));
}

/**
 * @brief 关闭目录句柄。
 * @param[in] directory 当前有效句柄。
 * @return OK 表示已关闭。
 */
Service_StatusTypeDef Service_Filesystem_CloseDirectory(
    Service_Filesystem_DirectoryHandleTypeDef directory)
{
    DIR *object;
    Service_StatusTypeDef status = filesystem_handle_lookup_directory(directory, &object);

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = filesystem_make_service_status(f_closedir(object));
    if (status == SERVICE_OK)
    {
        filesystem_handle_release_directory(directory);
    }
    return status;
}

/**
 * @brief 在所选卷上创建目录。
 * @param[in] volume 目标卷。
 * @param[in] path UTF-8 相对路径，不能为空。
 * @return OK 为创建成功；已存在映射为 BUSY。
 */
Service_StatusTypeDef Service_Filesystem_CreateDirectory(Service_Filesystem_VolumeTypeDef volume,
                                                         const char *path)
{
    TCHAR native_path[FILESYSTEM_TCHAR_PATH_LENGTH];
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

    return filesystem_make_service_status(f_mkdir(native_path));
}

/**
 * @brief 删除所选卷上的空目录。
 * @param[in] volume 目标卷。
 * @param[in] path UTF-8 相对路径，不能为空或根目录。
 * @return OK 为删除成功；非空目录返回 BUSY。
 */
Service_StatusTypeDef Service_Filesystem_RemoveDirectory(Service_Filesystem_VolumeTypeDef volume,
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
    if (result != FR_OK)
    {
        return filesystem_make_service_status(result);
    }
    if ((info.fattrib & AM_DIR) == 0U)
    {
        return SERVICE_INVALID_PARAM;
    }

    result = f_unlink(native_path);
    if (result == FR_DENIED)
    {
        return SERVICE_BUSY;
    }
    return filesystem_make_service_status(result);
}
