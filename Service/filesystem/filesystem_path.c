/**
 * @file filesystem_path.c
 * @brief 校验 UTF-8 相对路径，并加上所选卷的 FatFs 盘符。
 */

#include "Service/filesystem/filesystem_path.h"

#include "FATFS/App/fatfs.h"
#include "Service/filesystem/filesystem_config.h"
#include "Service/filesystem/filesystem_status.h"

#include <string.h>

/**
 * @brief 判断路径分隔后的单个分量是否为 "." 或 ".."。
 * @param[in] component 分量起始。
 * @param[in] length 分量字节数。
 * @return true 表示应拒绝的当前或父目录分量。
 */
static bool filesystem_path_is_dot_component(const char *component, uint32_t length)
{
    return ((length == 1U) && (component[0] == '.')) ||
           ((length == 2U) && (component[0] == '.') && (component[1] == '.'));
}

/**
 * @brief 从 UTF-8 解码一个 Unicode 码点，拒绝过长编码、代理项和截断序列。
 * @param[in,out] cursor 当前字节指针，成功后前进。
 * @param[in] end 路径结尾（不含）。
 * @param[out] code_point 解码得到的码点。
 * @return true 解码成功。
 */
static bool filesystem_path_decode_utf8(const char **cursor, const char *end, uint32_t *code_point)
{
    const unsigned char *bytes = (const unsigned char *)(*cursor);
    uint32_t remaining = (uint32_t)(end - *cursor);
    uint32_t value;
    uint32_t size;
    uint32_t minimum;

    if (remaining == 0U)
    {
        return false;
    }

    if (bytes[0] < 0x80U)
    {
        *code_point = bytes[0];
        *cursor += 1;
        return true;
    }

    if (bytes[0] < 0xC2U)
    {
        return false;
    }
    if (bytes[0] < 0xE0U)
    {
        size = 2U;
        minimum = 0x80U;
        value = (uint32_t)bytes[0] & 0x1FU;
    }
    else if (bytes[0] < 0xF0U)
    {
        size = 3U;
        minimum = 0x800U;
        value = (uint32_t)bytes[0] & 0x0FU;
    }
    else if (bytes[0] < 0xF5U)
    {
        size = 4U;
        minimum = 0x10000U;
        value = (uint32_t)bytes[0] & 0x07U;
    }
    else
    {
        return false;
    }

    if (remaining < size)
    {
        return false;
    }

    for (uint32_t index = 1U; index < size; index++)
    {
        if ((bytes[index] & 0xC0U) != 0x80U)
        {
            return false;
        }
        value = (value << 6U) | ((uint32_t)bytes[index] & 0x3FU);
    }

    if ((value < minimum) || (value > 0x10FFFFU) || ((value >= 0xD800U) && (value <= 0xDFFFU)))
    {
        return false;
    }

    *code_point = value;
    *cursor += size;
    return true;
}

/**
 * @brief 拒绝 FatFs 与路径契约都不允许的文件名字符。
 * @param[in] code_point Unicode 码点。
 * @return true 表示非法字符。
 */
static bool filesystem_path_is_forbidden_code(uint32_t code_point)
{
    if (code_point < 0x20U)
    {
        return true;
    }

    return (code_point == (uint32_t)'"') || (code_point == (uint32_t)'*') ||
           (code_point == (uint32_t)'/') || (code_point == (uint32_t)':') ||
           (code_point == (uint32_t)'<') || (code_point == (uint32_t)'>') ||
           (code_point == (uint32_t)'?') || (code_point == (uint32_t)'\\') ||
           (code_point == (uint32_t)'|');
}

/**
 * @brief 校验 UTF-8 相对路径并统计码点，不写入输出缓冲。
 * @param[in] path 非空路径。
 * @param[in] length 字节长度。
 * @return SERVICE_OK 或 SERVICE_INVALID_PARAM。
 */
static Service_StatusTypeDef filesystem_path_validate(const char *path, uint32_t length)
{
    uint32_t index = 0U;

    if ((length == 0U) || (length > SERVICE_FILESYSTEM_PATH_MAX_BYTES) || (path[0] == '/') ||
        (path[length - 1U] == '/'))
    {
        return SERVICE_INVALID_PARAM;
    }

    while (index < length)
    {
        uint32_t start = index;

        if (path[index] == '/')
        {
            return SERVICE_INVALID_PARAM;
        }

        while ((index < length) && (path[index] != '/'))
        {
            if ((path[index] == '\\') || (path[index] == ':'))
            {
                return SERVICE_INVALID_PARAM;
            }
            index++;
        }

        if ((index == start) || filesystem_path_is_dot_component(&path[start], index - start) ||
            (path[index - 1U] == '.') || (path[index - 1U] == ' '))
        {
            return SERVICE_INVALID_PARAM;
        }

        const char *cursor = &path[start];
        const char *end = &path[index];
        while (cursor < end)
        {
            uint32_t code_point;

            if (!filesystem_path_decode_utf8(&cursor, end, &code_point) ||
                filesystem_path_is_forbidden_code(code_point))
            {
                return SERVICE_INVALID_PARAM;
            }
        }

        if (index < length)
        {
            index++;
            if (index == length)
            {
                return SERVICE_INVALID_PARAM;
            }
        }
    }

    return SERVICE_OK;
}

/**
 * @brief 取得所选卷由 CubeMX 链接的 ASCII 盘符路径。
 * @param[in] volume 目标卷。
 * @return 形如 "0:/" 的 4 字节路径；卷非法时为 NULL。
 */
static const char *filesystem_path_drive(Service_Filesystem_VolumeTypeDef volume)
{
    if (volume == SERVICE_FILESYSTEM_VOLUME_SD)
    {
        return SDPath;
    }
    if (volume == SERVICE_FILESYSTEM_VOLUME_FLASH)
    {
        return USERPath;
    }
    return NULL;
}

Service_StatusTypeDef filesystem_path_make_tchar(Service_Filesystem_VolumeTypeDef volume,
                                                 const char *path,
                                                 bool allow_empty_root,
                                                 TCHAR *destination)
{
    const char *drive;
    TCHAR drive_path[FILESYSTEM_DRIVE_PATH_LENGTH];
    uint32_t length = 0U;
    uint32_t out_index = 3U;
    const char *cursor;
    const char *end;

    if ((destination == NULL) || (path == NULL))
    {
        return SERVICE_INVALID_PARAM;
    }

    drive = filesystem_path_drive(volume);
    if (!filesystem_make_drive_path(drive, drive_path))
    {
        return SERVICE_NOT_READY;
    }

    while (path[length] != '\0')
    {
        if (length >= SERVICE_FILESYSTEM_PATH_MAX_BYTES)
        {
            return SERVICE_INVALID_PARAM;
        }
        length++;
    }

    if (length == 0U)
    {
        if (!allow_empty_root)
        {
            return SERVICE_INVALID_PARAM;
        }
        destination[0] = drive_path[0];
        destination[1] = drive_path[1];
        destination[2] = drive_path[2];
        destination[3] = 0;
        return SERVICE_OK;
    }

    if (filesystem_path_validate(path, length) != SERVICE_OK)
    {
        return SERVICE_INVALID_PARAM;
    }

    destination[0] = drive_path[0];
    destination[1] = drive_path[1];
    destination[2] = drive_path[2];
    cursor = path;
    end = path + length;
    while (cursor < end)
    {
        uint32_t code_point;

        if (!filesystem_path_decode_utf8(&cursor, end, &code_point))
        {
            return SERVICE_INVALID_PARAM;
        }
        if (code_point == (uint32_t)'/')
        {
            if (out_index >= (FILESYSTEM_TCHAR_PATH_LENGTH - 1U))
            {
                return SERVICE_INVALID_PARAM;
            }
            destination[out_index++] = (TCHAR)'/';
            continue;
        }
        if (code_point <= 0xFFFFU)
        {
            if (out_index >= (FILESYSTEM_TCHAR_PATH_LENGTH - 1U))
            {
                return SERVICE_INVALID_PARAM;
            }
            destination[out_index++] = (TCHAR)code_point;
        }
        else
        {
            uint32_t extra = code_point - 0x10000U;
            if ((out_index + 1U) >= (FILESYSTEM_TCHAR_PATH_LENGTH - 1U))
            {
                return SERVICE_INVALID_PARAM;
            }
            destination[out_index++] = (TCHAR)(0xD800U + (extra >> 10U));
            destination[out_index++] = (TCHAR)(0xDC00U + (extra & 0x3FFU));
        }
    }
    destination[out_index] = 0;
    return SERVICE_OK;
}

bool filesystem_path_tchar_name_to_utf8(const TCHAR *source, char *destination)
{
    uint32_t out_index = 0U;
    uint32_t in_index = 0U;

    if ((source == NULL) || (destination == NULL))
    {
        return false;
    }

    while (source[in_index] != 0)
    {
        uint32_t code_point = (uint32_t)source[in_index++];
        uint32_t needed;
        unsigned char encoded[4];

        if ((code_point >= 0xD800U) && (code_point <= 0xDBFFU))
        {
            uint32_t low = (uint32_t)source[in_index];
            if ((low < 0xDC00U) || (low > 0xDFFFU))
            {
                return false;
            }
            in_index++;
            code_point = 0x10000U + (((code_point - 0xD800U) << 10U) | (low - 0xDC00U));
        }
        else if ((code_point >= 0xDC00U) && (code_point <= 0xDFFFU))
        {
            return false;
        }

        if (code_point < 0x80U)
        {
            needed = 1U;
            encoded[0] = (unsigned char)code_point;
        }
        else if (code_point < 0x800U)
        {
            needed = 2U;
            encoded[0] = (unsigned char)(0xC0U | (code_point >> 6U));
            encoded[1] = (unsigned char)(0x80U | (code_point & 0x3FU));
        }
        else if (code_point < 0x10000U)
        {
            needed = 3U;
            encoded[0] = (unsigned char)(0xE0U | (code_point >> 12U));
            encoded[1] = (unsigned char)(0x80U | ((code_point >> 6U) & 0x3FU));
            encoded[2] = (unsigned char)(0x80U | (code_point & 0x3FU));
        }
        else
        {
            needed = 4U;
            encoded[0] = (unsigned char)(0xF0U | (code_point >> 18U));
            encoded[1] = (unsigned char)(0x80U | ((code_point >> 12U) & 0x3FU));
            encoded[2] = (unsigned char)(0x80U | ((code_point >> 6U) & 0x3FU));
            encoded[3] = (unsigned char)(0x80U | (code_point & 0x3FU));
        }

        if ((out_index + needed) > SERVICE_FILESYSTEM_NAME_MAX_BYTES)
        {
            return false;
        }
        memcpy(&destination[out_index], encoded, needed);
        out_index += needed;
    }

    destination[out_index] = '\0';
    return true;
}
