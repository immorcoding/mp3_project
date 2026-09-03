/**
 * @file filesystem_path.h
 * @brief Filesystem Module 私有 UTF-8 相对路径校验与 TCHAR 转换。
 */

#ifndef FILESYSTEM_PATH_H
#define FILESYSTEM_PATH_H

#include <stdbool.h>
#include <stdint.h>
#include "Middlewares/Third_Party/FatFs/src/ff.h"
#include "Service/filesystem/filesystem_types.h"
#include "Service/service.h"

/** @brief 含盘符前缀的 TCHAR 路径缓冲长度，覆盖最长 UTF-8 相对路径。 */
#define FILESYSTEM_TCHAR_PATH_LENGTH (SERVICE_FILESYSTEM_PATH_MAX_BYTES + 4U)

/**
 * @brief 将所选卷上的 UTF-8 相对路径转换为 FatFs TCHAR 路径。
 * @param[in] volume 目标卷。
 * @param[in] path UTF-8 相对路径；目录根允许空字符串。
 * @param[in] allow_empty_root true 时空路径表示该卷根目录。
 * @param[out] destination 至少 FILESYSTEM_TCHAR_PATH_LENGTH 个 TCHAR。
 * @return SERVICE_OK 转换成功；INVALID_PARAM 路径或卷非法；NOT_READY 盘符未就绪。
 */
Service_StatusTypeDef filesystem_path_make_tchar(Service_Filesystem_VolumeTypeDef volume,
                                                 const char *path,
                                                 bool allow_empty_root,
                                                 TCHAR *destination);

/**
 * @brief 将 FAT 目录项中的 TCHAR 名称转为 UTF-8。
 * @param[in] source FatFs 提供的以 0 结尾的 TCHAR 名称。
 * @param[out] destination 至少 SERVICE_FILESYSTEM_NAME_MAX_BYTES+1 字节。
 * @return true 完整转换；false 参数无效或超出公开名称长度。
 */
bool filesystem_path_tchar_name_to_utf8(const TCHAR *source, char *destination);

#endif /* FILESYSTEM_PATH_H */
