/**
 ******************************************************************************
 * @file    filesystem_service.c
 * @brief   FatFs 卷生命周期的 Service 层入口。
 *
 * @details
 *          CubeMX 的 MX_FATFS_Init() 只负责把生成的 DiskIO Driver 连接到
 *          SDPath/USERPath；它在 main() 中调用一次。本模块不重复链接驱动。
 *          每个公开函数入口拿一次卷锁、返回前放（spike #191）；公开函数之间互相调用时
 *          （例如格式化里的卸载与挂载）走不加锁的 `_locked` 内部版本，因为
 *          `configUSE_RECURSIVE_MUTEXES` 为 0。`f_mount` 会碰 FatFs 全局锁表 `Files[]`，
 *          每卷一把锁时它在打开表锁内执行。
 ******************************************************************************
 */

#include "Service/filesystem/filesystem_service.h"
#include "Service/filesystem/filesystem_config.h"
#include "Service/filesystem/filesystem_handle.h"
#include "Service/filesystem/filesystem_lock.h"
#include "Service/filesystem/filesystem_status.h"

#include <stdint.h>

#include "FATFS/App/fatfs.h"
#include "Middlewares/Third_Party/FatFs/src/ff.h"
#include "Service/filesystem/sd/filesystem_sd_transfer.h"
#include "Service/filesystem/flash/filesystem_flash_transfer.h"

/** @brief 格式化期间使用的 Service 私有静态工作区；只在 Flash 卷锁内使用。 */
static uint8_t filesystem_mkfs_work_buffer[FILESYSTEM_MKFS_WORK_BUFFER_SIZE];

/**
 * @brief 打开底层 Flash 逻辑卷并重建映射，不建立 FAT 文件系统。
 * @retval SERVICE_OK FTL 已就绪；重复打开就绪卷不重新扫描。
 * @retval SERVICE_NO_FILESYSTEM 没有可识别的 FTL 格式。
 * @note 调用者持有 Flash 卷锁；不自动格式化。
 */
static Service_StatusTypeDef filesystem_flash_open_volume(void)
{
    if (!filesystem_flash_transfer_is_registered() || (retUSER != 0U) || (USERPath[0] == '\0'))
    {
        return SERVICE_NOT_READY;
    }

    return filesystem_flash_transfer_open();
}

/**
 * @brief  检查 CubeMX 是否已成功链接 SD DiskIO Driver，并建立 DMA 完成信号量与回调。
 * @param[in] notify_index 旧版任务通知槽，已不再使用。
 * @retval SERVICE_OK SD Driver 已由 main() 中的 MX_FATFS_Init() 链接，且执行器已就绪。
 * @retval SERVICE_NOT_READY 驱动尚未链接或执行器注册失败。
 * @note   调用者持有 SD 卷锁。本函数绝不再次调用 MX_FATFS_Init()，避免重复增加 FatFs 逻辑卷。
 */
static Service_StatusTypeDef filesystem_sd_init_locked(uint32_t notify_index)
{
    if ((retSD != 0U) || (SDPath[0] == '\0'))
    {
        filesystem_handle_set_volume_initialized(SERVICE_FILESYSTEM_VOLUME_SD, false);
        return SERVICE_NOT_READY;
    }

    if (!filesystem_sd_transfer_init(notify_index))
    {
        filesystem_handle_set_volume_initialized(SERVICE_FILESYSTEM_VOLUME_SD, false);
        return SERVICE_NOT_READY;
    }

    filesystem_handle_set_volume_initialized(SERVICE_FILESYSTEM_VOLUME_SD, true);
    return SERVICE_OK;
}

/**
 * @brief  检查 CubeMX 是否已成功链接 SD DiskIO Driver，并建立 DMA 完成信号量与回调。
 * @param[in] notify_index 旧版 Storage Task 通知槽，由 APP 枚举注入；已不再使用，保留参数只为不改调用方。
 * @retval SERVICE_OK SD Driver 已由 main() 中的 MX_FATFS_Init() 链接，且执行器已就绪。
 * @retval SERVICE_NOT_READY 驱动尚未链接或执行器注册失败。
 * @note   入口拿 SD 卷锁。本函数绝不再次调用 MX_FATFS_Init()，避免重复增加 FatFs 逻辑卷。
 */
Service_StatusTypeDef Service_Filesystem_InitSD(uint32_t notify_index)
{
    Service_StatusTypeDef status;

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "InitSD");
    status = filesystem_sd_init_locked(notify_index);
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);
    return status;
}

/**
 * @brief  在已绑定执行器的前提下，做幂等就绪检查。
 * @retval SERVICE_OK 执行器仍然可用。
 * @retval SERVICE_NOT_READY 尚未 InitSD，或再次注册失败。
 * @note   调用者持有 SD 卷锁。
 */
static Service_StatusTypeDef filesystem_sd_ensure_bound_locked(void)
{
    if (!filesystem_sd_transfer_is_registered())
    {
        return SERVICE_NOT_READY;
    }

    return filesystem_sd_init_locked(0U);
}

/**
 * @brief  强制挂载当前已就绪的 SD 逻辑卷。
 * @retval SERVICE_OK 挂载成功。
 * @retval SERVICE_BUSY 该卷仍有打开的文件或目录。
 * @retval SERVICE_NO_FILESYSTEM 介质不存在可挂载的 FAT 文件系统。
 * @note   入口拿 SD 卷锁；`f_mount` 在打开表锁内。本函数不初始化 Platform SD，也不格式化 SD。
 */
Service_StatusTypeDef Service_Filesystem_MountSD(void)
{
    TCHAR sd_drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];
    Service_StatusTypeDef status;
    FRESULT result;

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "MountSD");

    if (filesystem_handle_volume_has_open(SERVICE_FILESYSTEM_VOLUME_SD))
    {
        status = SERVICE_BUSY;
    }
    else if (filesystem_sd_ensure_bound_locked() != SERVICE_OK)
    {
        status = SERVICE_NOT_READY;
    }
    else if (!filesystem_make_drive_path(SDPath, sd_drive_path))
    {
        status = SERVICE_INVALID_PARAM;
    }
    else
    {
        filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_SD, false);
        filesystem_lock_table_acquire("MountSD");
        result = f_mount(&SDFatFS, sd_drive_path, 1U);
        filesystem_lock_table_release();
        filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_SD, result == FR_OK);
        status = filesystem_make_service_status(result);
    }

    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);
    return status;
}

/**
 * @brief  注销当前 SD 的 FatFs 卷对象，并使该卷全部文件/目录 Token 立即失效。
 * @retval SERVICE_OK 已成功注销。
 * @note   入口拿 SD 卷锁，所以会等在途的 SD 读取先返回；`f_mount` 在打开表锁内。
 *         调用 f_mount(NULL, ..., 0) 不访问已移除的 SD 卡。拔出路径即使 FatFs
 *         返回错误也会失效句柄，避免继续使用已消失的介质。
 */
Service_StatusTypeDef Service_Filesystem_UnmountSD(void)
{
    TCHAR sd_drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];
    Service_StatusTypeDef status;
    FRESULT result;

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "UnmountSD");

    if (filesystem_sd_ensure_bound_locked() != SERVICE_OK)
    {
        filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_SD, false);
        status = SERVICE_NOT_READY;
    }
    else if (!filesystem_make_drive_path(SDPath, sd_drive_path))
    {
        filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_SD, false);
        status = SERVICE_INVALID_PARAM;
    }
    else
    {
        filesystem_lock_table_acquire("UnmountSD");
        result = f_mount(NULL, sd_drive_path, 0U);
        filesystem_lock_table_release();
        filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_SD, false);
        status = filesystem_make_service_status(result);
    }

    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);
    return status;
}

/**
 * @brief 建立 Flash 执行器的完成提示信号量和长期事件订阅。
 * @param[in] notify_index 旧版任务通知槽，已不再使用，保留参数只为不改调用方。
 * @retval SERVICE_OK 绑定成功，或已绑定。
 * @retval SERVICE_NOT_READY 订阅失败。
 * @note 入口拿 Flash 卷锁；不扫描、不挂载，也不格式化。
 */
Service_StatusTypeDef Service_Filesystem_InitFlash(uint32_t notify_index)
{
    Service_StatusTypeDef status;

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_FLASH, "InitFlash");
    status = filesystem_flash_transfer_init(notify_index) ? SERVICE_OK : SERVICE_NOT_READY;
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);
    return status;
}

/**
 * @brief 打开 FTL 后挂载 USERPath 对应的 Flash FAT 卷。
 * @retval SERVICE_OK FTL 打开并且 FatFs 挂载成功。
 * @retval SERVICE_BUSY 该卷仍有打开的文件或目录。
 * @note 调用者持有 Flash 卷锁；`f_mount` 在打开表锁内。失败不执行格式化。
 */
static Service_StatusTypeDef filesystem_flash_mount_locked(void)
{
    TCHAR drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];
    Service_StatusTypeDef status;
    FRESULT result;

    if (filesystem_handle_volume_has_open(SERVICE_FILESYSTEM_VOLUME_FLASH))
    {
        return SERVICE_BUSY;
    }

    status = filesystem_flash_open_volume();
    if (status != SERVICE_OK)
    {
        return status;
    }
    if (!filesystem_make_drive_path(USERPath, drive_path))
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_FLASH, false);
    filesystem_lock_table_acquire("MountFlash");
    result = f_mount(&USERFatFS, drive_path, 1U);
    filesystem_lock_table_release();
    filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_FLASH, result == FR_OK);
    return filesystem_make_service_status(result);
}

/**
 * @brief 打开 FTL 后挂载 USERPath 对应的 Flash FAT 卷。
 * @retval SERVICE_OK FTL 打开并且 FatFs 挂载成功。
 * @retval SERVICE_BUSY 该卷仍有打开的文件或目录。
 * @note 入口拿 Flash 卷锁。失败不执行格式化。
 */
Service_StatusTypeDef Service_Filesystem_MountFlash(void)
{
    Service_StatusTypeDef status;

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_FLASH, "MountFlash");
    status = filesystem_flash_mount_locked();
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);
    return status;
}

/**
 * @brief 注销 Flash FatFs 卷对象，并使该卷全部 Token 立即失效。
 * @retval SERVICE_OK 卷对象已注销。
 * @note 调用者持有 Flash 卷锁；`f_mount` 在打开表锁内。不等价于 f_sync。
 */
static Service_StatusTypeDef filesystem_flash_unmount_locked(void)
{
    TCHAR drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];
    FRESULT result;

    if (!filesystem_flash_transfer_is_registered() || (retUSER != 0U) || (USERPath[0] == '\0'))
    {
        filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_FLASH, false);
        return SERVICE_NOT_READY;
    }
    if (!filesystem_make_drive_path(USERPath, drive_path))
    {
        filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_FLASH, false);
        return SERVICE_INVALID_PARAM;
    }

    filesystem_lock_table_acquire("UnmountFlash");
    result = f_mount(NULL, drive_path, 0U);
    filesystem_lock_table_release();
    filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_FLASH, false);
    return filesystem_make_service_status(result);
}

/**
 * @brief 注销 Flash FatFs 卷对象，并使该卷全部 Token 立即失效。
 * @retval SERVICE_OK 卷对象已注销。
 * @note 入口拿 Flash 卷锁；不等价于 f_sync。
 */
Service_StatusTypeDef Service_Filesystem_UnmountFlash(void)
{
    Service_StatusTypeDef status;

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_FLASH, "UnmountFlash");
    status = filesystem_flash_unmount_locked();
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);
    return status;
}

/**
 * @brief 显式重建 FTL 与 FAT，成功返回时 Flash 卷已经重新挂载。
 * @retval SERVICE_OK 两层格式化完成并且 FatFs 已挂载。
 * @warning 销毁 Flash 分区内文件；必须先确认分区及数据可被覆盖。
 * @note 入口拿一次 Flash 卷锁，内部的卸载与挂载走不加锁的版本。启动或挂载失败不会调用此入口。
 */
Service_StatusTypeDef Service_Filesystem_FormatAndMountFlash(void)
{
    TCHAR drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];
    Service_StatusTypeDef status;

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_FLASH, "FormatMount");

    status = filesystem_flash_unmount_locked();
    if (status != SERVICE_OK)
    {
        filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);
        return status;
    }
    if (!filesystem_make_drive_path(USERPath, drive_path))
    {
        filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);
        return SERVICE_INVALID_PARAM;
    }

    status = filesystem_flash_transfer_format();
    if (status == SERVICE_OK)
    {
        status = filesystem_make_service_status(f_mkfs(drive_path,
                                                       FM_FAT | FM_SFD,
                                                       0U,
                                                       filesystem_mkfs_work_buffer,
                                                       sizeof(filesystem_mkfs_work_buffer)));
    }
    if (status == SERVICE_OK)
    {
        status = filesystem_flash_mount_locked();
    }

    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);
    return status;
}

/**
 * @brief 给 FTL 一次有限回收机会，最多回收一个失效块。
 * @retval SERVICE_OK 本次回收完成，可能无需实际擦除。
 * @note 入口拿 Flash 卷锁，所以回收期间 Flash 上的其他调用要等。GC 策略属于 FTL。
 */
Service_StatusTypeDef Service_Filesystem_ReclaimFlash(void)
{
    Service_StatusTypeDef status;

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_FLASH, "ReclaimFlash");
    status = filesystem_flash_transfer_reclaim();
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);
    return status;
}

/**
 * @brief 显式注销旧卷、恢复硬件并重新扫描 FTL，成功时重新挂载。
 * @retval SERVICE_OK 硬件已就绪、FTL 重扫成功并且 FatFs 已挂载。
 * @note 入口拿一次 Flash 卷锁，内部的卸载与挂载走不加锁的版本。不自动格式化。旧文件对象不可沿用。
 */
Service_StatusTypeDef Service_Filesystem_RecoverAndMountFlash(void)
{
    Service_StatusTypeDef status;

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_FLASH, "RecoverMount");

    status = filesystem_flash_unmount_locked();
    if (status == SERVICE_OK)
    {
        status = filesystem_flash_transfer_recover();
    }
    if (status == SERVICE_OK)
    {
        status = filesystem_flash_mount_locked();
    }

    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);
    return status;
}
