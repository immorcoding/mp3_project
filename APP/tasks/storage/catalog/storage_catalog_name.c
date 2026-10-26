/**
 * @file storage_catalog_name.c
 * @brief 曲目显示名纯函数的实现：取文件主名、去 MP3 扩展名、校验 UTF-8。
 */

#include "storage_catalog_name.h"

#include <stddef.h>

/**
 * @brief 判断 [text, text + length) 是否为严格合法的 UTF-8（无 NUL、无过长编码、无代理项、不超 U+10FFFF）。
 * @param[in] text 文本起点。
 * @param[in] length 字节数。
 * @return true 合法。
 */
static bool catalog_name_utf8_valid(const char *text, uint32_t length)
{
    uint32_t i = 0U;

    while (i < length)
    {
        uint8_t lead = (uint8_t)text[i];
        uint32_t extra;
        uint32_t code;

        if (lead == 0U)
        {
            return false;
        }
        if (lead < 0x80U)
        {
            i++;
            continue;
        }
        if ((lead >= 0xC2U) && (lead <= 0xDFU))
        {
            extra = 1U;
            code = lead & 0x1FU;
        }
        else if ((lead >= 0xE0U) && (lead <= 0xEFU))
        {
            extra = 2U;
            code = lead & 0x0FU;
        }
        else if ((lead >= 0xF0U) && (lead <= 0xF4U))
        {
            extra = 3U;
            code = lead & 0x07U;
        }
        else
        {
            return false;
        }

        if ((length - i - 1U) < extra)
        {
            return false;
        }
        for (uint32_t k = 1U; k <= extra; k++)
        {
            uint8_t cont = (uint8_t)text[i + k];

            if ((cont & 0xC0U) != 0x80U)
            {
                return false;
            }
            code = (code << 6) | (uint32_t)(cont & 0x3FU);
        }

        if (((extra == 2U) && (code < 0x800U)) ||
            ((extra == 3U) && ((code < 0x10000U) || (code > 0x10FFFFU))) ||
            ((code >= 0xD800U) && (code <= 0xDFFFU)))
        {
            return false;
        }
        i += extra + 1U;
    }
    return true;
}

/**
 * @brief 判断名字是否以 `.mp3` 结尾，m/p 大小写任意。
 * @param[in] name 文件名起点；NULL 视为不匹配。
 * @param[in] length 文件名字节数。
 * @return true 以 `.mp3` 结尾（含恰为 `.mp3` 的名字）。
 * @note  曲库扫描的收录判断与显示名去扩展名共用本函数，两处不会各自漂移。
 */
bool storage_catalog_has_mp3_extension(const char *name, uint32_t length)
{
    if ((name == NULL) || (length < 4U) || (name[length - 4U] != '.'))
    {
        return false;
    }
    return ((name[length - 3U] == 'm') || (name[length - 3U] == 'M')) &&
           ((name[length - 2U] == 'p') || (name[length - 2U] == 'P')) &&
           (name[length - 1U] == '3');
}

/**
 * @brief 从曲库 UTF-8 相对路径提取曲目显示名。
 * @param[in] path 相对路径，不要求以 '\0' 结尾。
 * @param[in] path_bytes 路径字节数。
 * @param[out] out 输出缓冲；失败时写空串（容量至少 1 字节时）。
 * @param[in] out_bytes 输出容量，含结尾 '\0'；调用方统一传 STORAGE_CATALOG_NAME_BYTES（即 STOR-14 的 251 B 主名上限）。
 * @return true 成功；false 参数无效、主名为空、放不下或非法 UTF-8。
 * @note 取最后一个 '/' 之后的部分，只去掉末尾 `.mp3`；去掉后为空（文件名恰为 `.mp3`）时保留整名。
 */
bool storage_catalog_display_name(const char *path,
                                  uint32_t path_bytes,
                                  char *out,
                                  uint32_t out_bytes)
{
    uint32_t base = 0U;
    uint32_t length;

    if ((out == NULL) || (out_bytes == 0U))
    {
        return false;
    }
    out[0] = '\0';
    if (path == NULL)
    {
        return false;
    }

    for (uint32_t i = 0U; i < path_bytes; i++)
    {
        if (path[i] == '/')
        {
            base = i + 1U;
        }
    }
    length = path_bytes - base;
    if (length == 0U)
    {
        return false;
    }
    if (storage_catalog_has_mp3_extension(path + base, length) && (length > 4U))
    {
        length -= 4U;
    }

    if ((length >= out_bytes) || !catalog_name_utf8_valid(path + base, length))
    {
        return false;
    }
    for (uint32_t i = 0U; i < length; i++)
    {
        out[i] = path[base + i];
    }
    out[length] = '\0';
    return true;
}
