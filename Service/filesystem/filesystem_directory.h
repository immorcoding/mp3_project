/**
 * @file filesystem_directory.h
 * @brief Storage Task 独占的卷感知目录 Interface。
 */

#ifndef FILESYSTEM_DIRECTORY_H
#define FILESYSTEM_DIRECTORY_H

#include <stdbool.h>
#include <stdint.h>
#include "Service/filesystem/filesystem_types.h"
#include "Service/service.h"

/**
 * @brief 不透明目录句柄；Token 由 Service 分配，调用者不得自行构造。
 */
typedef struct
{
    uint32_t Token; /**< 零表示无效；关闭、卸载或恢复后旧值不得继续使用。 */
} Service_Filesystem_DirectoryHandleTypeDef;

/**
 * @brief 一次目录读取得到的目录项。
 * @note  `Name[0] == '\0'` 且返回 `SERVICE_OK` 表示已经读到目录结尾。
 */
typedef struct
{
    char Name[SERVICE_FILESYSTEM_NAME_MAX_BYTES + 1U]; /**< UTF-8 名称。 */
    uint64_t Size;       /**< 文件字节大小；目录通常为零。 */
    uint16_t Date;       /**< FAT 打包日期。 */
    uint16_t Time;       /**< FAT 打包时间。 */
    bool IsDirectory;    /**< 该目录项是子目录。 */
    bool IsReadOnly;     /**< 只读属性。 */
} Service_Filesystem_DirectoryEntryTypeDef;

Service_StatusTypeDef Service_Filesystem_OpenDirectory(
    Service_Filesystem_VolumeTypeDef volume,
    const char *path,
    Service_Filesystem_DirectoryHandleTypeDef *directory);
Service_StatusTypeDef Service_Filesystem_ReadDirectory(
    Service_Filesystem_DirectoryHandleTypeDef directory,
    Service_Filesystem_DirectoryEntryTypeDef *entry);
Service_StatusTypeDef Service_Filesystem_RewindDirectory(
    Service_Filesystem_DirectoryHandleTypeDef directory);
Service_StatusTypeDef Service_Filesystem_CloseDirectory(
    Service_Filesystem_DirectoryHandleTypeDef directory);
Service_StatusTypeDef Service_Filesystem_CreateDirectory(
    Service_Filesystem_VolumeTypeDef volume,
    const char *path);
Service_StatusTypeDef Service_Filesystem_RemoveDirectory(
    Service_Filesystem_VolumeTypeDef volume,
    const char *path);

#endif /* FILESYSTEM_DIRECTORY_H */
