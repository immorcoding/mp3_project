/**
 ******************************************************************************
 * @file    filesystem_service.c
 * @brief   FatFs 挂载与格式化的 Service 层入口。
 *
 * @details
 *          CubeMX 的 MX_FATFS_Init() 只负责把生成的 DiskIO Driver 连接到
 *          SDPath/USERPath；它在 main() 中调用一次。本模块不重复链接驱动，
 *          只在 Storage Task 所在的普通执行上下文调用 FatFs API。
 ******************************************************************************
 */

#include "Service/filesystem/filesystem_service.h"
#include "Service/filesystem/filesystem_config.h"

#include <stdbool.h>
#include <stdint.h>

#include "FATFS/App/fatfs.h"
#include "Middlewares/Third_Party/FatFs/src/ff.h"
#include "Service/filesystem/sd/filesystem_sd_transfer.h"
#include "Service/filesystem/flash/filesystem_flash_transfer.h"
#include "Service/filesystem/flash/filesystem_flash_file.h"

/** @brief 格式化期间使用的 Service 私有静态工作区。 */
static uint8_t filesystem_mkfs_work_buffer[FILESYSTEM_MKFS_WORK_BUFFER_SIZE];

/**
 * @brief  将 FatFs 的内部结果收敛为 Service 的公开操作结果。
 * @param  result FatFs API 返回的原始结果。
 * @return 调用者可据此作出流程决策的 Service 状态。
 */
static Service_StatusTypeDef filesystem_make_service_status(FRESULT result)
{
    switch (result)
    {
        case FR_OK:
            return SERVICE_OK;

        case FR_INVALID_PARAMETER:
            return SERVICE_INVALID_PARAM;

        case FR_NOT_READY:
            return SERVICE_NOT_READY;

        case FR_TIMEOUT:
            return SERVICE_TIMEOUT;

        case FR_LOCKED:
            return SERVICE_BUSY;

        case FR_NO_FILESYSTEM:
            return SERVICE_NO_FILESYSTEM;

        default:
            return SERVICE_ERROR;
    }
}

/**
 * @brief  将 CubeMX 生成的 ASCII 逻辑卷路径复制为 FatFs API 所需的 TCHAR 路径。
 * @param  source CubeMX 生成的 '\0' 结尾 ASCII 路径，例如 "0:/"。
 * @param  destination 接收 TCHAR 路径的固定长度缓冲区。
 * @retval true 路径完整复制。
 * @retval false 参数无效或路径没有在固定缓冲区内结束。
 * @note   当前 _LFN_UNICODE 为 1，TCHAR 是 UTF-16，不能把 char * 直接强转为
 *         TCHAR *。逻辑卷路径只包含 ASCII 字符，因此逐字符提升是安全的。
 */
static bool filesystem_make_drive_path(
    const char *source,
    TCHAR *destination)
{
    uint32_t index;

    if ((source == NULL) || (destination == NULL))
    {
        return false;
    }

    for (index = 0U; index < FILESYSTEM_DRIVE_PATH_LENGTH; index++)
    {
        destination[index] = (TCHAR)(uint8_t)source[index];

        if (source[index] == '\0')
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief  检查 CubeMX 是否已成功链接 SD DiskIO Driver。
 * @retval SERVICE_OK SD Driver 已由 main() 中的 MX_FATFS_Init() 链接。
 * @retval SERVICE_NOT_READY 驱动尚未链接或链接失败。
 * @note   本函数绝不再次调用 MX_FATFS_Init()，避免重复增加 FatFs 逻辑卷。
 */
Service_StatusTypeDef Service_Filesystem_Init(void)
{
    if ((retSD != 0U) || (SDPath[0] == '\0'))
    {
        return SERVICE_NOT_READY;
    }

    return filesystem_sd_transfer_init() ? SERVICE_OK : SERVICE_NOT_READY;
}

/**
 * @brief  将当前 SD 逻辑卷格式化为 FAT32。
 * @retval SERVICE_OK 格式化完成。
 * @retval SERVICE_INVALID_PARAM 逻辑卷路径无效。
 * @retval SERVICE_TIMEOUT、SERVICE_BUSY 或 SERVICE_ERROR FatFs 操作失败。
 * @warning 格式化会销毁卷中现有文件；仅应在 Storage Service 已取得 SD 独占权
 *          且上层明确确认后调用。
 */
Service_StatusTypeDef Service_Filesystem_FormatSD(void)
{
    TCHAR sd_drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];

    if (!filesystem_make_drive_path(SDPath, sd_drive_path))
    {
        return SERVICE_INVALID_PARAM;
    }

    return filesystem_make_service_status(
        f_mkfs(sd_drive_path,
               FM_FAT32,
               0U,
               filesystem_mkfs_work_buffer,
               sizeof(filesystem_mkfs_work_buffer)));
}

/**
 * @brief  强制挂载当前已就绪的 SD 逻辑卷。
 * @retval SERVICE_OK 挂载成功。
 * @retval SERVICE_NO_FILESYSTEM 介质不存在可挂载的 FAT 文件系统。
 * @retval SERVICE_INVALID_PARAM、SERVICE_NOT_READY、SERVICE_TIMEOUT、
 *         SERVICE_BUSY 或 SERVICE_ERROR 挂载失败。
 * @note   本函数不初始化 Platform SD；调用者必须先经过 Storage Task 的卡检测、
 *         消抖和 Platform_SD_Init()/Process() 生命周期。
 */
Service_StatusTypeDef Service_Filesystem_MountSD(void)
{
    TCHAR sd_drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];

    if (!filesystem_make_drive_path(SDPath, sd_drive_path))
    {
        return SERVICE_INVALID_PARAM;
    }

    return filesystem_make_service_status(f_mount(&SDFatFS, sd_drive_path, 1U));
}

/**
 * @brief  注销当前 SD 逻辑卷的 FatFs 卷对象。
 * @retval SERVICE_OK 已成功注销。
 * @retval SERVICE_INVALID_PARAM、SERVICE_NOT_READY、SERVICE_TIMEOUT、
 *         SERVICE_BUSY 或 SERVICE_ERROR 注销失败。
 * @note   调用 f_mount(NULL, ..., 0) 只解除逻辑卷与 FATFS 对象的关联；它不会
 *         对已经移除的 SD 卡发起块访问，因此可用于热拔出收尾。
 */
Service_StatusTypeDef Service_Filesystem_UnmountSD(void)
{
    TCHAR sd_drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];

    if (!filesystem_make_drive_path(SDPath, sd_drive_path))
    {
        return SERVICE_INVALID_PARAM;
    }

    return filesystem_make_service_status(f_mount(NULL, sd_drive_path, 0U));
}


/**
 * @brief 在当前 Storage Task 建立唯一 Flash 执行器和长期事件订阅。
 * @retval SERVICE_OK 绑定成功，或已由同一任务绑定。
 * @retval SERVICE_NOT_READY 无有效任务、订阅失败或其他任务已持有执行器。
 * @note 只在 Storage Task 普通上下文调用；不扫描、不挂载，也不格式化。
 *       诊断与逻辑卷共用此执行器，APP 不再建立第二条 QSPI 回调。
 */
Service_StatusTypeDef Service_Filesystem_InitFlash(void)
{
    return filesystem_flash_transfer_init() ? SERVICE_OK : SERVICE_NOT_READY;
}

/**
 * @brief 打开底层逻辑卷并重建映射，不建立 FAT 文件系统。
 * @retval SERVICE_OK FTL 已就绪；重复打开就绪卷不重新扫描。
 * @retval SERVICE_NO_FILESYSTEM 没有可识别的 FTL 格式。
 * @retval SERVICE_NOT_READY 调用者/Driver Link 未就绪，或卷不完整、不兼容。
 * @retval SERVICE_TIMEOUT 已触发超时并完成安全收尾。
 * @retval SERVICE_ERROR 扫描、校验或硬件失败。
 * @note 仅初始化后的 Storage Task 调用，同步等待期间让出 CPU；不自动格式化。
 */
Service_StatusTypeDef Service_Filesystem_OpenFlash(void)
{
    if (!filesystem_flash_transfer_is_owner() || retUSER != 0 || !USERPath[0])
    {
        return SERVICE_NOT_READY;
    }
    return filesystem_flash_transfer_open();
}

/**
 * @brief 打开 FTL 后挂载 USERPath 对应的 Flash FAT 卷。
 * @retval SERVICE_OK FTL 打开并且 FatFs 挂载成功。
 * @retval SERVICE_BUSY 文件槽仍占用，拒绝重新挂载以免使在用文件失效。
 * @retval SERVICE_NO_FILESYSTEM 缺少 FTL 格式或可挂载的 FAT 文件系统。
 * @return 其他 Service 状态表示打开或挂载失败，保留未就绪/超时/错误语义。
 * @note 仅初始化后的 Storage Task 调用，可能同步等待；失败不执行格式化。
 */
Service_StatusTypeDef Service_Filesystem_MountFlash(void)
{
    TCHAR drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];
    if (filesystem_flash_file_is_open())
    {
        return SERVICE_BUSY;
    }
    Service_StatusTypeDef status = Service_Filesystem_OpenFlash();
    if (status != SERVICE_OK)
    {
        return status;
    }
    if (!filesystem_make_drive_path(USERPath, drive_path))
    {
        return SERVICE_INVALID_PARAM;
    }
    filesystem_flash_file_set_mounted(false);
    FRESULT result = f_mount(&USERFatFS, drive_path, 1);
    filesystem_flash_file_set_mounted(result == FR_OK);
    return filesystem_make_service_status(result);
}

/**
 * @brief 注销 Flash FatFs 卷对象，不写介质。
 * @retval SERVICE_OK 卷对象已注销。
 * @retval SERVICE_NOT_READY 非所有者上下文或 USER Driver Link 未就绪。
 * @return 其他 Service 状态表示路径或 FatFs 注销失败。
 * @note 仅 Storage Task 调用；不等价于 f_sync，调用前由上层结束正常文件访问。
 *       先前打开的文件对象不可在注销后继续使用。
 */
Service_StatusTypeDef Service_Filesystem_UnmountFlash(void)
{
    TCHAR drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];
    if (!filesystem_flash_transfer_is_owner() || retUSER != 0 || !USERPath[0])
    {
        return SERVICE_NOT_READY;
    }
    if (!filesystem_make_drive_path(USERPath, drive_path))
    {
        return SERVICE_INVALID_PARAM;
    }
    FRESULT result = f_mount(NULL, drive_path, 0);
    if (result == FR_OK)
    {
        filesystem_flash_file_set_mounted(false);
    }
    return filesystem_make_service_status(result);
}

/**
 * @brief 显式重建 FTL 格式，再建立 FAT12/16 superfloppy 文件系统。
 * @retval SERVICE_OK 两层格式化完成，卷仍未自动挂载。
 * @return 其他 Service 状态表示注销、FTL 格式化或 FatFs 格式化失败。
 * @warning 销毁 Flash 分区内文件；必须先确认分区及数据可被覆盖。
 * @note 仅初始化后的 Storage Task 调用，可能长期同步等待并让出 CPU。
 *       启动或挂载失败不会调用此入口；成功后需显式 MountFlash。
 *       失败可能留下不完整 FTL 或未建立 FAT 的卷，不自动重试。
 */
Service_StatusTypeDef Service_Filesystem_FormatFlash(void)
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
    /* 约 19 MiB 使用 FatFs 自动选择 FAT12/16，单卷 superfloppy 无额外 MBR。 */
    return filesystem_make_service_status(f_mkfs(drive_path,
                                                 FM_FAT | FM_SFD,
                                                 0,
                                                 filesystem_mkfs_work_buffer,
                                                 sizeof(filesystem_mkfs_work_buffer)));
}

/**
 * @brief 给 FTL 一次有限维护机会，最多回收一个失效块。
 * @retval SERVICE_OK 本次维护完成，可能无需实际擦除。
 * @retval SERVICE_NOT_READY 非所有者上下文或卷未就绪，不发起维护。
 * @return 其他 Service 状态表示传输或安全收尾失败。
 * @note 仅 Storage Task 在空闲机会调用。GC 策略属于 FTL，已发起擦除不可抢占。
 */
Service_StatusTypeDef Service_Filesystem_MaintainFlash(void)
{
    return filesystem_flash_transfer_maintain();
}

/**
 * @brief 显式注销旧卷、恢复硬件并重新扫描 FTL。
 * @retval SERVICE_OK 硬件已就绪且 FTL 重扫成功，尚未重新挂载。
 * @return 其他 Service 状态表示注销、WIP/QE 核验或扫描失败。
 * @note 仅 Storage Task 调用；同步等待并让出 CPU，不自动格式化或重挂载。
 *       故障前文件对象必须丢弃，成功后显式挂载并重新打开文件。
 */
Service_StatusTypeDef Service_Filesystem_RecoverFlash(void)
{
    Service_StatusTypeDef status = Service_Filesystem_UnmountFlash();
    if (status != SERVICE_OK)
    {
        return status;
    }
    return filesystem_flash_transfer_recover();
}
