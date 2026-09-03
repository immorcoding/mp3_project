/**
 * @file filesystem_file.h
 * @brief Storage Task 独占的卷感知文件 Interface。
 */

#ifndef FILESYSTEM_FILE_H
#define FILESYSTEM_FILE_H

#include <stdbool.h>
#include <stdint.h>
#include "Service/filesystem/filesystem_types.h"
#include "Service/service.h"

/**
 * @brief 不透明文件句柄；Token 由 Service 分配，调用者不得自行构造。
 */
typedef struct
{
    uint32_t Token; /**< 零表示无效；关闭、卸载或恢复后旧值不得继续使用。 */
} Service_Filesystem_FileHandleTypeDef;

/**
 * @brief 文件打开方式。
 * @note  不提供隐式截断已有文件的 CREATE_ALWAYS。
 */
typedef enum
{
    SERVICE_FILESYSTEM_FILE_MODE_READ = 0, /**< 打开已有文件，只读；目标不存在则失败。 */
    SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, /**< 新建可读可写文件；同名已存在则返回 SERVICE_BUSY，不截断或覆盖。 */
    SERVICE_FILESYSTEM_FILE_MODE_READ_WRITE, /**< 打开已有文件，可读可写；目标不存在则失败。 */
    SERVICE_FILESYSTEM_FILE_MODE_APPEND      /**< 打开已有文件并定位到末尾，可读可写；目标不存在则失败。 */
} Service_Filesystem_FileModeTypeDef;

/**
 * @brief 路径查询得到的文件或目录元数据。
 * @note  不泄漏 FatFs FILINFO；时间字段沿用 FAT 打包日期/时间。
 */
typedef struct
{
    uint64_t Size;       /**< 字节大小；目录大小由文件系统给出，通常为零。 */
    uint16_t Date;       /**< FAT 打包日期。 */
    uint16_t Time;       /**< FAT 打包时间。 */
    bool IsDirectory;    /**< 目标是目录。 */
    bool IsReadOnly;     /**< 只读属性。 */
} Service_Filesystem_FileInfoTypeDef;

Service_StatusTypeDef Service_Filesystem_OpenFile(
    Service_Filesystem_VolumeTypeDef volume,
    const char *path,
    Service_Filesystem_FileModeTypeDef mode,
    Service_Filesystem_FileHandleTypeDef *file);
Service_StatusTypeDef Service_Filesystem_ReadFile(
    Service_Filesystem_FileHandleTypeDef file,
    void *buffer,
    uint32_t requested,
    uint32_t *transferred);
Service_StatusTypeDef Service_Filesystem_WriteFile(
    Service_Filesystem_FileHandleTypeDef file,
    const void *buffer,
    uint32_t requested,
    uint32_t *transferred);
Service_StatusTypeDef Service_Filesystem_SeekFile(
    Service_Filesystem_FileHandleTypeDef file,
    uint64_t offset);
Service_StatusTypeDef Service_Filesystem_GetFilePosition(
    Service_Filesystem_FileHandleTypeDef file,
    uint64_t *position);
Service_StatusTypeDef Service_Filesystem_GetFileSize(
    Service_Filesystem_FileHandleTypeDef file,
    uint64_t *size);
Service_StatusTypeDef Service_Filesystem_SyncFile(Service_Filesystem_FileHandleTypeDef file);
Service_StatusTypeDef Service_Filesystem_CloseFile(Service_Filesystem_FileHandleTypeDef file);
Service_StatusTypeDef Service_Filesystem_RemoveFile(
    Service_Filesystem_VolumeTypeDef volume,
    const char *path);
Service_StatusTypeDef Service_Filesystem_RenameFile(
    Service_Filesystem_VolumeTypeDef volume,
    const char *old_path,
    const char *new_path);
Service_StatusTypeDef Service_Filesystem_GetFileInfo(
    Service_Filesystem_VolumeTypeDef volume,
    const char *path,
    Service_Filesystem_FileInfoTypeDef *info);

#endif /* FILESYSTEM_FILE_H */
