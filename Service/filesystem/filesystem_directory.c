/**
 * @file filesystem_directory.c
 * @brief 卷感知目录访问；FatFs DIR 对象不向 APP 暴露。
 */

#include "Service/filesystem/filesystem_directory.h"

#include "Middlewares/Third_Party/FatFs/src/diskio.h"
#include "Service/filesystem/filesystem_entry.h"
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

_Static_assert(SERVICE_FILESYSTEM_VOLUME_FLASH < FILESYSTEM_VOLUME_COUNT, "one sector buffer per volume");

/** @brief 取回目录项所在扇区用的缓冲，避免占任务栈。每卷一份：只在所属卷的卷锁内使用，两卷的 ReadDirectory 可以同时进行而不共用它。 */
static BYTE filesystem_directory_sector_buffer[FILESYSTEM_VOLUME_COUNT][_MAX_SS];

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
 * @param[in] volume 目录所在的卷；决定用哪一份扇区缓冲。
 * @param[in] info `f_readdir` 给出的条目信息，用来与目录项原始字节互相核对。
 * @return 定位符；位置无法确定、读扇区失败或原始字节与 `info` 不一致时为全零（无定位符），
 *         调用者按路径打开即可，不影响目录读取本身的结果。
 * @note FAT12/16/32 均可，exFAT 不支持（未启用）。目录项扇区通常仍在 FatFs 窗口里直接取；
 *       只在 `dir_next` 因跨簇把窗口换成 FAT 扇区时才多读一次该扇区。
 */
static Service_Filesystem_FileLocatorTypeDef filesystem_directory_capture_locator(
    const DIR *dir,
    DWORD before_clust,
    DWORD before_dptr,
    Service_Filesystem_VolumeTypeDef volume,
    const FILINFO *info)
{
    const FATFS *fs = dir->obj.fs;
    Service_Filesystem_FileLocatorTypeDef none = {{0U, 0U, 0U, 0U}};
    const BYTE *entry;
    DWORD offset;
    DWORD sector;
    uint32_t start_cluster;

    if ((fs == NULL) || (fs->fs_type < FS_FAT12) || (fs->fs_type > FS_FAT32) ||
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
        if (disk_read(fs->drv, filesystem_directory_sector_buffer[volume], sector, 1U) != RES_OK)
        {
            return none;
        }
        entry = &filesystem_directory_sector_buffer[volume][offset % _MAX_SS];
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
 * @brief 持卷锁时占槽并打开目录；只有 `f_opendir` 在窄表锁内。
 * @param[in] volume 目标卷，调用者已持其卷锁。
 * @param[in] native_path 已转换好的 TCHAR 路径。
 * @param[out] directory 仅成功时写入有效句柄。
 * @return 见 `Service_Filesystem_OpenDirectory`。
 */
static Service_StatusTypeDef filesystem_directory_open_locked(
    Service_Filesystem_VolumeTypeDef volume,
    const TCHAR *native_path,
    Service_Filesystem_DirectoryHandleTypeDef *directory)
{
    DIR *object;
    Service_Filesystem_DirectoryHandleTypeDef handle = {0};
    FRESULT result;
    Service_StatusTypeDef status = filesystem_handle_acquire_directory(volume, &object, &handle);

    if (status != SERVICE_OK)
    {
        return status;
    }

    /* 卷锁 → 表锁。f_opendir 给子目录登记锁表项，和 f_open 一样会写跨卷共用的 Files[]。 */
    filesystem_lock_table_take(FILESYSTEM_OP_OPEN_DIR);
    result = f_opendir(object, native_path);
    filesystem_lock_table_give();
    if (result != FR_OK)
    {
        filesystem_handle_release_directory(handle);
        return filesystem_make_service_status(result);
    }

    *directory = handle;
    return SERVICE_OK;
}

/**
 * @brief 打开所选卷上的目录；空路径表示该卷根目录。
 * @param[in] volume 目标卷。
 * @param[in] path UTF-8 相对路径，空字符串表示根目录。
 * @param[out] directory 仅成功时写入有效句柄。
 * @return OK 为打开成功；BUSY 为该卷的目录槽用尽。
 * @note  拿所属卷的卷锁一次；`f_opendir` 一步在窄表锁内。
 */
Service_StatusTypeDef Service_Filesystem_OpenDirectory(
    Service_Filesystem_VolumeTypeDef volume,
    const char *path,
    Service_Filesystem_DirectoryHandleTypeDef *directory)
{
    TCHAR native_path[FILESYSTEM_TCHAR_PATH_LENGTH];
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (directory == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }

    /* 路径转换是纯计算；卷非法时它返回 NOT_READY，所以下面的 volume 一定合法。 */
    status = filesystem_path_make_tchar(volume, path, true, native_path);
    if (status != SERVICE_OK)
    {
        return status;
    }

    status = filesystem_entry_enter(volume, FILESYSTEM_OP_OPEN_DIR, SERVICE_NOT_READY);
    if (status != SERVICE_OK)
    {
        return status;
    }
    status = filesystem_directory_open_locked(volume, native_path, directory);
    return filesystem_entry_leave(volume, status);
}

/**
 * @brief 持卷锁时读下一个目录项。
 * @param[in] directory 当前句柄。
 * @param[out] entry 接收 UTF-8 名称与属性。
 * @param[in] volume 句柄所属卷，决定定位符采集用哪份扇区缓冲。
 * @return 见 `Service_Filesystem_ReadDirectory`。
 */
static Service_StatusTypeDef filesystem_directory_read_locked(
    Service_Filesystem_DirectoryHandleTypeDef directory,
    Service_Filesystem_DirectoryEntryTypeDef *entry,
    Service_Filesystem_VolumeTypeDef volume)
{
    DIR *object;
    FILINFO info;
    Service_StatusTypeDef status = filesystem_handle_lookup_directory(directory, &object);

    if (status != SERVICE_OK)
    {
        return status;
    }

    DWORD before_clust = object->clust;
    DWORD before_dptr = object->dptr;
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
    entry->Locator = filesystem_directory_capture_locator(object, before_clust, before_dptr, volume, &info);

    entry->Size = (uint64_t)info.fsize;
    entry->Date = info.fdate;
    entry->Time = info.ftime;
    entry->IsDirectory = ((info.fattrib & AM_DIR) != 0U);
    entry->IsReadOnly = ((info.fattrib & AM_RDO) != 0U);
    return SERVICE_OK;
}

/**
 * @brief 读取下一个目录项。
 * @param[in] directory 当前有效句柄。
 * @param[out] entry 接收 UTF-8 名称与属性；读到结尾时 Name 为空且仍返回 OK。
 * @return OK 为本次读取无错误。
 * @note  拿所属卷的卷锁一次，句柄有效性在锁内检查。跳过 FatFs 返回的空项之外的转换失败视为 ERROR。
 *        条目同时带文件定位符（见 `Service_Filesystem_OpenFileByLocator`）；取不到时 `Locator` 为全零。
 *        定位符采集可能多读一次目录项所在扇区，仍在同一次持锁里。
 */
Service_StatusTypeDef Service_Filesystem_ReadDirectory(
    Service_Filesystem_DirectoryHandleTypeDef directory,
    Service_Filesystem_DirectoryEntryTypeDef *entry)
{
    Service_Filesystem_VolumeTypeDef volume;
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (entry == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    memset(entry, 0, sizeof(*entry));
    if (!filesystem_handle_directory_volume(directory, &volume))
    {
        return SERVICE_INVALID_HANDLE;
    }

    status = filesystem_entry_enter(volume, FILESYSTEM_OP_READ_DIR, SERVICE_INVALID_HANDLE);
    if (status != SERVICE_OK)
    {
        return status;
    }
    status = filesystem_directory_read_locked(directory, entry, volume);
    return filesystem_entry_leave(volume, status);
}

/**
 * @brief 将目录读取位置回到开头。
 * @param[in] directory 当前有效句柄。
 * @return OK 为复位成功。
 * @note  拿所属卷的卷锁一次。
 */
Service_StatusTypeDef Service_Filesystem_RewindDirectory(
    Service_Filesystem_DirectoryHandleTypeDef directory)
{
    Service_Filesystem_VolumeTypeDef volume;
    DIR *object;
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (!filesystem_handle_directory_volume(directory, &volume))
    {
        return SERVICE_INVALID_HANDLE;
    }

    status = filesystem_entry_enter(volume, FILESYSTEM_OP_REWIND, SERVICE_INVALID_HANDLE);
    if (status != SERVICE_OK)
    {
        return status;
    }
    status = filesystem_handle_lookup_directory(directory, &object);
    if (status == SERVICE_OK)
    {
        status = filesystem_make_service_status(f_readdir(object, NULL));
    }
    return filesystem_entry_leave(volume, status);
}

/**
 * @brief 关闭目录句柄。
 * @param[in] directory 当前有效句柄。
 * @return OK 表示已关闭。
 * @note  拿所属卷的卷锁一次，关闭目录与释放槽在同一次持锁里；`f_closedir` 不在表锁内。
 */
Service_StatusTypeDef Service_Filesystem_CloseDirectory(
    Service_Filesystem_DirectoryHandleTypeDef directory)
{
    Service_Filesystem_VolumeTypeDef volume;
    DIR *object;
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (!filesystem_handle_directory_volume(directory, &volume))
    {
        return SERVICE_INVALID_HANDLE;
    }

    status = filesystem_entry_enter(volume, FILESYSTEM_OP_CLOSE_DIR, SERVICE_INVALID_HANDLE);
    if (status != SERVICE_OK)
    {
        return status;
    }
    status = filesystem_handle_lookup_directory(directory, &object);
    if (status == SERVICE_OK)
    {
        status = filesystem_make_service_status(f_closedir(object));
        if (status == SERVICE_OK)
        {
            filesystem_handle_release_directory(directory);
        }
    }
    return filesystem_entry_leave(volume, status);
}

/**
 * @brief 持卷锁时创建目录。
 * @param[in] volume 目标卷，调用者已持其卷锁。
 * @param[in] path UTF-8 相对路径。
 * @return 见 `Service_Filesystem_CreateDirectory`。
 */
static Service_StatusTypeDef filesystem_directory_create_locked(Service_Filesystem_VolumeTypeDef volume,
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
 * @brief 在所选卷上创建目录。
 * @param[in] volume 目标卷。
 * @param[in] path UTF-8 相对路径，不能为空。
 * @return OK 为创建成功；已存在映射为 BUSY。
 * @note  拿所属卷的卷锁一次。
 */
Service_StatusTypeDef Service_Filesystem_CreateDirectory(Service_Filesystem_VolumeTypeDef volume,
                                                         const char *path)
{
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (!filesystem_lock_volume_valid(volume))
    {
        return SERVICE_INVALID_PARAM;
    }

    status = filesystem_entry_enter(volume, FILESYSTEM_OP_MKDIR, SERVICE_NOT_READY);
    if (status != SERVICE_OK)
    {
        return status;
    }
    status = filesystem_directory_create_locked(volume, path);
    return filesystem_entry_leave(volume, status);
}

/**
 * @brief 持卷锁时删除空目录。
 * @param[in] volume 目标卷，调用者已持其卷锁。
 * @param[in] path UTF-8 相对路径。
 * @return 见 `Service_Filesystem_RemoveDirectory`。
 */
static Service_StatusTypeDef filesystem_directory_remove_locked(Service_Filesystem_VolumeTypeDef volume,
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

/**
 * @brief 删除所选卷上的空目录。
 * @param[in] volume 目标卷。
 * @param[in] path UTF-8 相对路径，不能为空或根目录。
 * @return OK 为删除成功；非空目录返回 BUSY。
 * @note  拿所属卷的卷锁一次，`f_stat` 与 `f_unlink` 之间不放。
 */
Service_StatusTypeDef Service_Filesystem_RemoveDirectory(Service_Filesystem_VolumeTypeDef volume,
                                                         const char *path)
{
    Service_StatusTypeDef status;

    filesystem_lock_check_caller();
    if (!filesystem_lock_volume_valid(volume))
    {
        return SERVICE_INVALID_PARAM;
    }

    status = filesystem_entry_enter(volume, FILESYSTEM_OP_RMDIR, SERVICE_NOT_READY);
    if (status != SERVICE_OK)
    {
        return status;
    }
    status = filesystem_directory_remove_locked(volume, path);
    return filesystem_entry_leave(volume, status);
}
