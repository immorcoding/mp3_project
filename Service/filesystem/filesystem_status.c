/**
 * @file filesystem_status.c
 * @brief 将 FatFs 结果与盘符路径收敛为 Service 语义。
 */

#include "Service/filesystem/filesystem_status.h"

#include <stdint.h>

/** @brief 必须与 `filesystem_config.h` 的 `FILESYSTEM_DRIVE_PATH_LENGTH` 一致。 */
#define FILESYSTEM_STATUS_DRIVE_PATH_LENGTH 4U

/**
 * @brief 将 FatFs 的内部结果收敛为 Service 的公开操作结果。
 * @param result FatFs API 返回的原始结果。
 * @return 调用者可据此作出流程决策的 Service 状态。
 */
Service_StatusTypeDef filesystem_make_service_status(FRESULT result)
{
    switch (result)
    {
        case FR_OK:
            return SERVICE_OK;

        case FR_EXIST:
        case FR_LOCKED:
            return SERVICE_BUSY;

        case FR_NOT_READY:
            return SERVICE_NOT_READY;

        case FR_TIMEOUT:
            return SERVICE_TIMEOUT;

        case FR_NO_FILESYSTEM:
            return SERVICE_NO_FILESYSTEM;

        case FR_INVALID_OBJECT:
        case FR_INVALID_NAME:
        case FR_INVALID_PARAMETER:
            return SERVICE_INVALID_PARAM;

        default:
            return SERVICE_ERROR;
    }
}

/**
 * @brief 将 CubeMX 生成的 ASCII 逻辑卷路径复制为 FatFs API 所需的 TCHAR 路径。
 * @param source CubeMX 生成的 '\0' 结尾 ASCII 路径，例如 "0:/"。
 * @param destination 接收 TCHAR 路径的固定长度缓冲区。
 * @retval true 路径完整复制。
 * @retval false 参数无效或路径没有在固定缓冲区内结束。
 * @note 当前 _LFN_UNICODE 为 1，TCHAR 是 UTF-16，不能把 char * 直接强转为
 *       TCHAR *。逻辑卷路径只包含 ASCII 字符，因此逐字符提升是安全的。
 */
bool filesystem_make_drive_path(const char *source, TCHAR *destination)
{
    uint32_t index;

    if ((source == NULL) || (destination == NULL))
    {
        return false;
    }

    for (index = 0U; index < FILESYSTEM_STATUS_DRIVE_PATH_LENGTH; index++)
    {
        destination[index] = (TCHAR)(uint8_t)source[index];

        if (source[index] == '\0')
        {
            return true;
        }
    }

    return false;
}
