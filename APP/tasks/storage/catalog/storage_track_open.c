/**
 * @file storage_track_open.c
 * @brief 曲目文件的打开：按文件定位符打开，核对不符退回按路径打开并记 Locator miss。
 */

#include "storage_track_open.h"

#include "storage_catalog.h"
#include "storage_track_diag.h"

/**
 * @brief 打开一首曲目的 SD 文件（只读）。
 * @param[in] position 播放列表位置，只用于核对不符时的 `Locator miss` 日志。
 * @param[in] catalog_index 曲目的 Catalog 下标，用来取文件定位符。
 * @param[in] path 曲库 UTF-8 相对路径，核对不符时按它打开。
 * @param[out] file 仅返回 SERVICE_OK 时写入有效句柄。
 * @param[out] open_us 打开耗时，单位 µs，含核对不符后退回路径打开的那一次，不含 `Locator miss` 日志投递；失败时同样给出。
 * @return SERVICE_OK 已打开；其他值为打开失败的 Service 状态。
 * @note  只有“核对不符”（无定位符、条目已变）才退回按路径打开，并记一行 WARN、累加计数器；
 *        槽用尽、卷未就绪、介质错误等其他失败不退回，直接把原因交给调用者（退回路径打开通常无益）。
 *        调用者须已核对 Sheet 与 Catalog 代次一致，定位符才与本曲库代次的条目对应。
 */
Service_StatusTypeDef storage_track_open(uint16_t position,
                                         uint16_t catalog_index,
                                         const char *path,
                                         Service_Filesystem_FileHandleTypeDef *file,
                                         uint32_t *open_us)
{
    uint32_t start_cycles = storage_track_diag_begin();
    uint32_t locator_us = 0U;
    Service_Filesystem_FileLocatorTypeDef locator;
    Service_StatusTypeDef status;

    if (storage_catalog_copy_locator(catalog_index, &locator))
    {
        Service_StatusTypeDef locator_status = SERVICE_OK;

        switch (Service_Filesystem_OpenFileByLocator(SERVICE_FILESYSTEM_VOLUME_SD, &locator, file, &locator_status))
        {
            case SERVICE_FILESYSTEM_LOCATOR_OPENED:
                *open_us = storage_track_diag_elapsed_us(start_cycles);
                return SERVICE_OK;
            case SERVICE_FILESYSTEM_LOCATOR_FAILED:
                *open_us = storage_track_diag_elapsed_us(start_cycles);
                return (locator_status != SERVICE_OK) ? locator_status : SERVICE_ERROR;
            case SERVICE_FILESYSTEM_LOCATOR_MISMATCH:
            default:
                locator_us = storage_track_diag_elapsed_us(start_cycles);   /* 先停表再记日志：投递耗时不计入打开。 */
                break;
        }
    }

    storage_track_diag_locator_miss(position);
    start_cycles = storage_track_diag_begin();
    status = Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                         path,
                                         SERVICE_FILESYSTEM_FILE_MODE_READ,
                                         file);
    *open_us = locator_us + storage_track_diag_elapsed_us(start_cycles);
    return status;
}
