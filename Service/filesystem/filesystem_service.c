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

#include <stdbool.h>
#include <stdint.h>

#include "FATFS/App/fatfs.h"

/** @brief CubeMX 生成的逻辑卷路径固定为 "N:/" 加结尾 '\0'。 */
#define FILESYSTEM_DRIVE_PATH_LENGTH        4U

/** @brief 当前 FatFs 配置使用 512 B 扇区，格式化工作区无需占用任务栈。 */
#define FILESYSTEM_MKFS_WORK_BUFFER_SIZE    512U

/** @brief 格式化期间使用的 Service 私有静态工作区。 */
static uint8_t filesystem_mkfs_work_buffer[FILESYSTEM_MKFS_WORK_BUFFER_SIZE];

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
  * @retval FR_OK SD Driver 已由 main() 中的 MX_FATFS_Init() 链接。
  * @retval FR_NOT_READY 驱动尚未链接或链接失败。
  * @note   本函数绝不再次调用 MX_FATFS_Init()，避免重复增加 FatFs 逻辑卷。
  */
FRESULT Filesystem_Init(void)
{
    if ((retSD != 0U) || (SDPath[0] == '\0'))
    {
        return FR_NOT_READY;
    }

    return FatFs_SD_BindCurrentTask() ? FR_OK : FR_INT_ERR;
}

/**
  * @brief  将当前 SD 逻辑卷格式化为 FAT32。
  * @retval FatFs 原始 FRESULT，调用者必须显式处理失败。
  * @warning 格式化会销毁卷中现有文件；仅应在 Storage Service 已取得 SD 独占权
  *          且上层明确确认后调用。
  */
FRESULT Filesystem_FormatSD(void)
{
    TCHAR sd_drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];

    if (!filesystem_make_drive_path(SDPath, sd_drive_path))
    {
        return FR_INVALID_PARAMETER;
    }

    return f_mkfs(sd_drive_path,
                  FM_FAT32,
                  0U,
                  filesystem_mkfs_work_buffer,
                  sizeof(filesystem_mkfs_work_buffer));
}

/**
  * @brief  强制挂载当前已就绪的 SD 逻辑卷。
  * @retval FatFs 原始 FRESULT，FR_OK 表示挂载成功。
  * @note   本函数不初始化 Platform SD；调用者必须先经过 Storage Task 的卡检测、
  *         消抖和 Platform_SD_Init()/Process() 生命周期。
  */
FRESULT Filesystem_MountSD(void)
{
    TCHAR sd_drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];

    if (!filesystem_make_drive_path(SDPath, sd_drive_path))
    {
        return FR_INVALID_PARAMETER;
    }

    return f_mount(&SDFatFS, sd_drive_path, 1U);
}

/**
  * @brief  注销当前 SD 逻辑卷的 FatFs 卷对象。
  * @retval FatFs 原始 FRESULT，FR_OK 表示已成功注销。
  * @note   调用 f_mount(NULL, ..., 0) 只解除逻辑卷与 FATFS 对象的关联；它不会
  *         对已经移除的 SD 卡发起块访问，因此可用于热拔出收尾。
  */
FRESULT Filesystem_UnmountSD(void)
{
    TCHAR sd_drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];

    if (!filesystem_make_drive_path(SDPath, sd_drive_path))
    {
        return FR_INVALID_PARAMETER;
    }

    return f_mount(NULL, sd_drive_path, 0U);
}
