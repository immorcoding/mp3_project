/**
 * @file filesystem_directory.c
 * @brief 卷感知目录访问；FatFs DIR 对象不向 APP 暴露。
 */

#include "Service/filesystem/filesystem_directory.h"

#include "Middlewares/Third_Party/FatFs/src/diskio.h"
#include "Service/filesystem/filesystem_handle.h"
#include "Service/filesystem/filesystem_lock.h"
#include "Service/filesystem/filesystem_locator.h"
#include "Service/filesystem/filesystem_path.h"
#include "Service/filesystem/filesystem_status.h"

#include <string.h>

/* 以下是 FAT 磁盘格式事实（FAT 规范与 ff.c 内部的 DIR_*、MAX_DIR，FatFs 不公开这些宏），集中在此，不随配置变化。 */
/** @brief FAT 目录项大小（字节）。 */
#define FILESYSTEM_DIRECTORY_ENTRY_BYTES 32U
/** @brief FatFs 对 FAT 目录的大小上限（字节），与 ff.c 的 MAX_DIR 一致。 */
#define FILESYSTEM_DIRECTORY_FAT_MAX_BYTES 0x200000UL
/** @brief 目录项内字段偏移，与 FAT 规范和 ff.c 的 DIR_* 一致。 */
#define FILESYSTEM_DIRECTORY_ENTRY_NAME 0U
#define FILESYSTEM_DIRECTORY_ENTRY_ATTR 11U
#define FILESYSTEM_DIRECTORY_ENTRY_CLUSTER_HIGH 20U
#define FILESYSTEM_DIRECTORY_ENTRY_CLUSTER_LOW 26U
#define FILESYSTEM_DIRECTORY_ENTRY_SIZE 28U
/** @brief 已删除目录项的首字节标记。 */
#define FILESYSTEM_DIRECTORY_ENTRY_DELETED 0xE5U
/** @brief 目录项属性的有效位掩码与长名项属性值。 */
#define FILESYSTEM_DIRECTORY_ATTR_MASK 0x3FU
#define FILESYSTEM_DIRECTORY_ATTR_LONG_NAME 0x0FU

_Static_assert(_MAX_SS == _MIN_SS, "locator capture assumes one fixed sector size");

/**
 * @brief 取回目录项所在扇区用的缓冲，避免占任务栈。
 * @note  按逻辑盘号各一份：两个卷的 ReadDirectory 在每卷一把锁时可能同时进行，共用一份会互相覆盖
 *        （#188 第 6 项）。每份只在对应卷的卷锁内使用。
 */
static BYTE filesystem_directory_sector_buffer[_VOLUMES][_MAX_SS];

/**
 * @brief 小端读 16 位。
 * @param[in] bytes 至少 2 字节。
 * @return 值。
 */
static uint32_t filesystem_directory_load16(const BYTE *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

/**
 * @brief 小端读 32 位。
 * @param[in] bytes 至少 4 字节。
 * @return 值。
 */
static uint32_t filesystem_directory_load32(const BYTE *bytes)
{
    return filesystem_directory_load16(bytes) | (filesystem_directory_load16(bytes + 2U) << 16U);
}

/**
 * @brief 在刚读出一个目录项之后，推算它的主（短名）目录项位置：字节偏移、所在簇与扇区。
 * @param[in] dir 刚由 `f_readdir` 读出条目的目录对象；`f_readdir` 已把游标移到下一项。
 * @param[in] before_clust 本次 `f_readdir` 之前的游标所在簇。
 * @param[in] before_dptr 本次 `f_readdir` 之前的游标偏移。
 * @param[out] offset 主目录项在文件夹内的字节偏移。
 * @param[out] sector 主目录项所在的绝对扇区。
 * @return true 位置可确定；false 为极少数无法确定的位置（如目录已满的最后两项）。
 * @note 目录读到末尾时 `dir_next` 报告结束且不推进游标（sect 为 0），主目录项就是游标处；
 *       否则游标在其后一项。一次读取最多跨一个簇边界（簇不小于 4 KiB 时），故主目录项的簇只能是
 *       读取前的簇或读取后的簇，按簇序号比较即可区分。
 */
static bool filesystem_directory_entry_position(const DIR *dir,
                                                DWORD before_clust,
                                                DWORD before_dptr,
                                                DWORD *offset,
                                                DWORD *sector)
{
    const FATFS *fs = dir->obj.fs;
    DWORD cluster_bytes = (DWORD)fs->csize * _MAX_SS;
    DWORD cluster;
    DWORD entry_offset;

    if (dir->sect == 0U)
    {
        entry_offset = dir->dptr;
        cluster = dir->clust;
    }
    else
    {
        if ((dir->dptr < FILESYSTEM_DIRECTORY_ENTRY_BYTES) ||
            (dir->dptr >= (FILESYSTEM_DIRECTORY_FAT_MAX_BYTES - FILESYSTEM_DIRECTORY_ENTRY_BYTES)))
        {
            return false;
        }
        entry_offset = dir->dptr - FILESYSTEM_DIRECTORY_ENTRY_BYTES;
        cluster = dir->clust;
        if ((dir->clust != 0U) && ((entry_offset / cluster_bytes) != (dir->dptr / cluster_bytes)))
        {
            cluster = before_clust;
            if ((entry_offset / cluster_bytes) != (before_dptr / cluster_bytes))
            {
                return false;
            }
        }
    }

    if (cluster == 0U)
    {
        *sector = fs->dirbase + (entry_offset / _MAX_SS);
    }
    else
    {
        if ((cluster < 2U) || (cluster >= fs->n_fatent))
        {
            return false;
        }
        *sector = fs->database + ((DWORD)fs->csize * (cluster - 2U)) +
                  ((entry_offset % cluster_bytes) / _MAX_SS);
    }
    *offset = entry_offset;
    return true;
}

/**
 * @brief 为刚读出的目录项取文件定位符。
 * @param[in] dir 刚由 `f_readdir` 读出条目的目录对象。
 * @param[in] before_clust 本次 `f_readdir` 之前的游标所在簇。
 * @param[in] before_dptr 本次 `f_readdir` 之前的游标偏移。
 * @param[in] info `f_readdir` 给出的条目信息，用来与目录项原始字节互相核对。
 * @return 定位符；位置无法确定、读扇区失败或原始字节与 `info` 不一致时为全零（无定位符），
 *         调用者按路径打开即可，不影响目录读取本身的结果。
 * @note FAT12/16/32 均可，exFAT 不支持（未启用）。目录项扇区通常仍在 FatFs 窗口里直接取；
 *       只在 `dir_next` 因跨簇把窗口换成 FAT 扇区时才多读一次该扇区。
 */
static Service_Filesystem_FileLocatorTypeDef filesystem_directory_capture_locator(
    const DIR *dir, DWORD before_clust, DWORD before_dptr, const FILINFO *info)
{
    const FATFS *fs = dir->obj.fs;
    Service_Filesystem_FileLocatorTypeDef none = {{0U, 0U, 0U, 0U}};
    const BYTE *entry;
    DWORD offset;
    DWORD sector;
    uint32_t start_cluster;

    if ((fs == NULL) || (fs->fs_type < FS_FAT12) || (fs->fs_type > FS_FAT32) ||
        (fs->drv >= _VOLUMES) ||
        !filesystem_directory_entry_position(dir, before_clust, before_dptr, &offset, &sector))
    {
        return none;
    }

    if (fs->winsect == sector)
    {
        entry = &fs->win[offset % _MAX_SS];
    }
    else
    {
        BYTE *sector_buffer = filesystem_directory_sector_buffer[fs->drv];

        if (disk_read(fs->drv, sector_buffer, sector, 1U) != RES_OK)
        {
            return none;
        }
        entry = &sector_buffer[offset % _MAX_SS];
    }

    if ((entry[FILESYSTEM_DIRECTORY_ENTRY_NAME] == 0U) ||
        (entry[FILESYSTEM_DIRECTORY_ENTRY_NAME] == FILESYSTEM_DIRECTORY_ENTRY_DELETED) ||
        (entry[FILESYSTEM_DIRECTORY_ENTRY_ATTR] == FILESYSTEM_DIRECTORY_ATTR_LONG_NAME) ||
        ((entry[FILESYSTEM_DIRECTORY_ENTRY_ATTR] & FILESYSTEM_DIRECTORY_ATTR_MASK) != (info->fattrib & FILESYSTEM_DIRECTORY_ATTR_MASK)) ||
        (filesystem_directory_load32(&entry[FILESYSTEM_DIRECTORY_ENTRY_SIZE]) != (uint32_t)info->fsize))
    {
        return none;
    }

    start_cluster = filesystem_directory_load16(&entry[FILESYSTEM_DIRECTORY_ENTRY_CLUSTER_LOW]);
    if (fs->fs_type == FS_FAT32)
    {
        start_cluster |= filesystem_directory_load16(&entry[FILESYSTEM_DIRECTORY_ENTRY_CLUSTER_HIGH]) << 16U;
    }
    return filesystem_locator_make(dir->obj.sclust, offset, start_cluster, (uint32_t)info->fsize);
}

/**
 * @brief 打开所选卷上的目录；空路径表示该卷根目录。
 * @param[in] volume 目标卷。
 * @param[in] path UTF-8 相对路径，空字符串表示根目录。
 * @param[out] directory 仅成功时写入有效句柄。
 * @return OK 为打开成功；BUSY 为目录槽用尽。
 * @note 入口拿卷锁；`f_opendir` 会登记 FatFs 锁表，每卷一把锁时在打开表锁内。
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
    FRESULT result;

    if ((directory == NULL) || !filesystem_handle_volume_is_valid(volume))
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_lock_acquire(volume, "OpenDirectory");

    status = filesystem_path_make_tchar(volume, path, true, native_path);
    if (status == SERVICE_OK)
    {
        status = filesystem_handle_acquire_directory(volume, &object, &handle);
    }
    if (status == SERVICE_OK)
    {
        filesystem_lock_table_acquire("OpenDirectory");
        result = f_opendir(object, native_path);
        filesystem_lock_table_release();
        if (result != FR_OK)
        {
            filesystem_handle_release_directory(handle);
            status = filesystem_make_service_status(result);
        }
        else
        {
            *directory = handle;
        }
    }

    filesystem_lock_release(volume);
    return status;
}

/**
 * @brief 读取下一个目录项。
 * @param[in] directory 当前有效句柄。
 * @param[out] entry 接收 UTF-8 名称与属性；读到结尾时 Name 为空且仍返回 OK。
 * @return OK 为本次读取无错误。
 * @note 入口拿 Token 所属卷的卷锁。跳过 FatFs 返回的空项之外的转换失败视为 ERROR。
 *       条目同时带文件定位符（见 `Service_Filesystem_OpenFileByLocator`）；取不到时 `Locator` 为全零。
 */
Service_StatusTypeDef Service_Filesystem_ReadDirectory(
    Service_Filesystem_DirectoryHandleTypeDef directory,
    Service_Filesystem_DirectoryEntryTypeDef *entry)
{
    DIR *object;
    Service_Filesystem_VolumeTypeDef volume;
    FILINFO info;
    Service_StatusTypeDef status;
    DWORD before_clust;
    DWORD before_dptr;
    FRESULT result;

    if (entry == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    memset(entry, 0, sizeof(*entry));
    status = filesystem_handle_lock_directory(directory, "ReadDirectory", &object, &volume);
    if (status != SERVICE_OK)
    {
        return status;
    }

    before_clust = object->clust;
    before_dptr = object->dptr;
    result = f_readdir(object, &info);
    if (result != FR_OK)
    {
        status = filesystem_make_service_status(result);
    }
    else if (info.fname[0] == 0)
    {
        status = SERVICE_OK;
    }
    else if (!filesystem_path_tchar_name_to_utf8(info.fname, entry->Name))
    {
        status = SERVICE_ERROR;
    }
    else
    {
        entry->Locator = filesystem_directory_capture_locator(object, before_clust, before_dptr, &info);

        entry->Size = (uint64_t)info.fsize;
        entry->Date = info.fdate;
        entry->Time = info.ftime;
        entry->IsDirectory = ((info.fattrib & AM_DIR) != 0U);
        entry->IsReadOnly = ((info.fattrib & AM_RDO) != 0U);
        status = SERVICE_OK;
    }

    filesystem_lock_release(volume);
    return status;
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
    Service_Filesystem_VolumeTypeDef volume;
    Service_StatusTypeDef status =
        filesystem_handle_lock_directory(directory, "RewindDirectory", &object, &volume);

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = filesystem_make_service_status(f_readdir(object, NULL));
    filesystem_lock_release(volume);
    return status;
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
    Service_Filesystem_VolumeTypeDef volume;
    Service_StatusTypeDef status =
        filesystem_handle_lock_directory(directory, "CloseDirectory", &object, &volume);
    FRESULT result;

    if (status != SERVICE_OK)
    {
        return status;
    }

    filesystem_lock_table_wide_acquire("CloseDirectory");
    result = f_closedir(object);
    filesystem_lock_table_wide_release();
    status = filesystem_make_service_status(result);
    if (status == SERVICE_OK)
    {
        filesystem_handle_release_directory(directory);
    }
    filesystem_lock_release(volume);
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
    Service_StatusTypeDef status;

    if (!filesystem_handle_volume_is_valid(volume))
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_lock_acquire(volume, "CreateDirectory");
    status = filesystem_handle_volume_ready(volume);
    if (status == SERVICE_OK)
    {
        status = filesystem_path_make_tchar(volume, path, false, native_path);
    }
    if (status == SERVICE_OK)
    {
        status = filesystem_make_service_status(f_mkdir(native_path));
    }
    filesystem_lock_release(volume);
    return status;
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
    Service_StatusTypeDef status;
    FRESULT result;

    if (!filesystem_handle_volume_is_valid(volume))
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_lock_acquire(volume, "RemoveDirectory");
    status = filesystem_handle_volume_ready(volume);
    if (status == SERVICE_OK)
    {
        status = filesystem_path_make_tchar(volume, path, false, native_path);
    }
    if (status == SERVICE_OK)
    {
        result = f_stat(native_path, &info);
        if (result != FR_OK)
        {
            status = filesystem_make_service_status(result);
        }
        else if ((info.fattrib & AM_DIR) == 0U)
        {
            status = SERVICE_INVALID_PARAM;
        }
        else
        {
            filesystem_lock_table_wide_acquire("RemoveDirectory");
            result = f_unlink(native_path);
            filesystem_lock_table_wide_release();
            status = (result == FR_DENIED) ? SERVICE_BUSY : filesystem_make_service_status(result);
        }
    }
    filesystem_lock_release(volume);
    return status;
}
