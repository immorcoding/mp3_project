/**
 ******************************************************************************
 * @file    filesystem_service.c
 * @brief   FatFs 卷生命周期的 Service 层入口。
 *
 * @details
 *          CubeMX 的 MX_FATFS_Init() 只负责把生成的 DiskIO Driver 连接到
 *          SDPath/USERPath；它在 main() 中调用一次。本模块不重复链接驱动，
 *          只在 Storage Task 所在的普通执行上下文调用 FatFs API。
 ******************************************************************************
 */

#include "Service/filesystem/filesystem_service.h"
#include "Service/filesystem/filesystem_config.h"
#include "Service/filesystem/filesystem_handle.h"
#include "Service/filesystem/filesystem_status.h"

#include <stdint.h>

#include "FATFS/App/fatfs.h"
#include "Middlewares/Third_Party/FatFs/src/ff.h"
#include "Service/filesystem/sd/filesystem_sd_transfer.h"
#include "Service/filesystem/flash/filesystem_flash_transfer.h"

/** @brief 格式化期间使用的 Service 私有静态工作区。 */
static uint8_t filesystem_mkfs_work_buffer[FILESYSTEM_MKFS_WORK_BUFFER_SIZE];

/**
 * @brief 打开底层 Flash 逻辑卷并重建映射，不建立 FAT 文件系统。
 * @retval SERVICE_OK FTL 已就绪；重复打开就绪卷不重新扫描。
 * @retval SERVICE_NO_FILESYSTEM 没有可识别的 FTL 格式。
 * @note 仅在 InitFlash 之后调用；不自动格式化。
 */
static Service_StatusTypeDef filesystem_flash_open_volume(void)
{
    if (!filesystem_flash_transfer_is_ready() || (retUSER != 0U) || (USERPath[0] == '\0'))
    {
        return SERVICE_NOT_READY;
    }

    return filesystem_flash_transfer_open();
}

/**
 * @brief  检查 CubeMX 是否已成功链接 SD DiskIO Driver，并建立 DMA 完成信号量与订阅。
 * @retval SERVICE_OK SD Driver 已由 main() 中的 MX_FATFS_Init() 链接，且完成信号量与订阅就绪。
 * @retval SERVICE_NOT_READY 驱动尚未链接，或 Platform SD 尚不能接收传输订阅。
 * @note   本函数绝不再次调用 MX_FATFS_Init()，避免重复增加 FatFs 逻辑卷。
 *         不绑定调用任务：之后任何任务都可以执行 SD 传输。第一次调用须在任何任务使用
 *         文件接口之前完成；挂载/卸载再次进入时只重新订阅，不重建信号量。
 */
Service_StatusTypeDef Service_Filesystem_InitSD(void)
{
    if ((retSD != 0U) || (SDPath[0] == '\0'))
    {
        filesystem_handle_set_volume_initialized(SERVICE_FILESYSTEM_VOLUME_SD, false);
        return SERVICE_NOT_READY;
    }

    if (!filesystem_sd_transfer_init())
    {
        filesystem_handle_set_volume_initialized(SERVICE_FILESYSTEM_VOLUME_SD, false);
        return SERVICE_NOT_READY;
    }

    filesystem_handle_set_volume_initialized(SERVICE_FILESYSTEM_VOLUME_SD, true);
    return SERVICE_OK;
}

/**
 * @brief  在 InitSD 已建立执行器的前提下做幂等就绪检查（重新订阅传输事件）。
 * @retval SERVICE_OK SD 执行器仍可接收 DMA 事件。
 * @retval SERVICE_NOT_READY 尚未 InitSD，或重新订阅失败。
 */
static Service_StatusTypeDef filesystem_sd_ensure_ready(void)
{
    if (!filesystem_sd_transfer_is_ready())
    {
        return SERVICE_NOT_READY;
    }

    return Service_Filesystem_InitSD();
}

/**
 * @brief  强制挂载当前已就绪的 SD 逻辑卷。
 * @retval SERVICE_OK 挂载成功。
 * @retval SERVICE_BUSY 该卷仍有打开的文件或目录。
 * @retval SERVICE_NO_FILESYSTEM 介质不存在可挂载的 FAT 文件系统。
 * @note   本函数不初始化 Platform SD，也不格式化 SD。
 */
Service_StatusTypeDef Service_Filesystem_MountSD(void)
{
    TCHAR sd_drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];

    if (filesystem_handle_volume_has_open(SERVICE_FILESYSTEM_VOLUME_SD))
    {
        return SERVICE_BUSY;
    }

    if (filesystem_sd_ensure_ready() != SERVICE_OK)
    {
        return SERVICE_NOT_READY;
    }
    if (!filesystem_make_drive_path(SDPath, sd_drive_path))
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_SD, false);
    FRESULT result = f_mount(&SDFatFS, sd_drive_path, 1U);
    filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_SD, result == FR_OK);
    return filesystem_make_service_status(result);
}

/**
 * @brief  注销当前 SD 的 FatFs 卷对象，并使该卷全部文件/目录 Token 立即失效。
 * @retval SERVICE_OK 已成功注销。
 * @note   调用 f_mount(NULL, ..., 0) 不访问已移除的 SD 卡。拔出路径即使 FatFs
 *         返回错误也会失效句柄，避免继续使用已消失的介质。
 */
Service_StatusTypeDef Service_Filesystem_UnmountSD(void)
{
    TCHAR sd_drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];
    FRESULT result;

    if (filesystem_sd_ensure_ready() != SERVICE_OK)
    {
        filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_SD, false);
        return SERVICE_NOT_READY;
    }
    if (!filesystem_make_drive_path(SDPath, sd_drive_path))
    {
        filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_SD, false);
        return SERVICE_INVALID_PARAM;
    }

    result = f_mount(NULL, sd_drive_path, 0U);
    filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_SD, false);
    return filesystem_make_service_status(result);
}

/**
 * @brief 建立 Flash 完成信号量和长期事件订阅，并把 Flash 卷标记为已初始化。
 * @retval SERVICE_OK 订阅成功，或已经订阅过。
 * @retval SERVICE_NOT_READY 信号量创建或 Platform 订阅失败。
 * @note 不绑定调用任务：之后任何任务都可以执行 Flash 请求。第一次调用须在任何任务使用
 *       文件接口之前完成；不扫描、不挂载，也不格式化。
 */
Service_StatusTypeDef Service_Filesystem_InitFlash(void)
{
    bool ready = filesystem_flash_transfer_init();

    filesystem_handle_set_volume_initialized(SERVICE_FILESYSTEM_VOLUME_FLASH, ready);
    return ready ? SERVICE_OK : SERVICE_NOT_READY;
}

/**
 * @brief 打开 FTL 后挂载 USERPath 对应的 Flash FAT 卷。
 * @retval SERVICE_OK FTL 打开并且 FatFs 挂载成功。
 * @retval SERVICE_BUSY 该卷仍有打开的文件或目录。
 * @note 失败不执行格式化。
 */
Service_StatusTypeDef Service_Filesystem_MountFlash(void)
{
    TCHAR drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];
    Service_StatusTypeDef status;

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
    FRESULT result = f_mount(&USERFatFS, drive_path, 1U);
    filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_FLASH, result == FR_OK);
    return filesystem_make_service_status(result);
}

/**
 * @brief 注销 Flash FatFs 卷对象，并使该卷全部 Token 立即失效。
 * @retval SERVICE_OK 卷对象已注销。
 * @note 不等价于 f_sync。
 */
Service_StatusTypeDef Service_Filesystem_UnmountFlash(void)
{
    TCHAR drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];
    FRESULT result;

    if (!filesystem_flash_transfer_is_ready() || (retUSER != 0U) || (USERPath[0] == '\0'))
    {
        filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_FLASH, false);
        return SERVICE_NOT_READY;
    }
    if (!filesystem_make_drive_path(USERPath, drive_path))
    {
        filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_FLASH, false);
        return SERVICE_INVALID_PARAM;
    }

    result = f_mount(NULL, drive_path, 0U);
    filesystem_handle_set_mounted(SERVICE_FILESYSTEM_VOLUME_FLASH, false);
    return filesystem_make_service_status(result);
}

/**
 * @brief 显式重建 FTL 与 FAT，成功返回时 Flash 卷已经重新挂载。
 * @retval SERVICE_OK 两层格式化完成并且 FatFs 已挂载。
 * @warning 销毁 Flash 分区内文件；必须先确认分区及数据可被覆盖。
 * @note 启动或挂载失败不会调用此入口。
 */
Service_StatusTypeDef Service_Filesystem_FormatAndMountFlash(void)
{
    TCHAR drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];
    Service_StatusTypeDef status = Service_Filesystem_UnmountFlash();

    if (status != SERVICE_OK)
    {
        return status;
    }
    if (!filesystem_make_drive_path(USERPath, drive_path))
    {
        return SERVICE_INVALID_PARAM;
    }

    status = filesystem_flash_transfer_format();
    if (status != SERVICE_OK)
    {
        return status;
    }

    status = filesystem_make_service_status(f_mkfs(drive_path,
                                                   FM_FAT | FM_SFD,
                                                   0U,
                                                   filesystem_mkfs_work_buffer,
                                                   sizeof(filesystem_mkfs_work_buffer)));
    if (status != SERVICE_OK)
    {
        return status;
    }

    return Service_Filesystem_MountFlash();
}

/**
 * @brief 给 FTL 一次有限回收机会，最多回收一个失效块。
 * @retval SERVICE_OK 本次回收完成，可能无需实际擦除。
 * @note 仅 Storage Task 在空闲机会调用。GC 策略属于 FTL。
 */
Service_StatusTypeDef Service_Filesystem_ReclaimFlash(void)
{
    return filesystem_flash_transfer_reclaim();
}

/**
 * @brief 显式注销旧卷、恢复硬件并重新扫描 FTL，成功时重新挂载。
 * @retval SERVICE_OK 硬件已就绪、FTL 重扫成功并且 FatFs 已挂载。
 * @note 仅 Storage Task 调用；不自动格式化。旧文件对象不可沿用。
 */
Service_StatusTypeDef Service_Filesystem_RecoverAndMountFlash(void)
{
    Service_StatusTypeDef status = Service_Filesystem_UnmountFlash();

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = filesystem_flash_transfer_recover();
    if (status != SERVICE_OK)
    {
        return status;
    }

    return Service_Filesystem_MountFlash();
}
