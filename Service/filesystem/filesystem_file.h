/**
 * @file filesystem_file.h
 * @brief 卷感知文件 Interface：线程安全，GUI 与播放任务不得调用（`Service_Filesystem_DenyCaller`）。
 *
 * @details
 *          每个函数在入口拿所属卷的锁、返回前放（一次调用只拿一次），调用可能阻塞到前一个持锁者
 *          放锁为止，最长见 `Service/filesystem/README.md` 的持锁上界表。句柄有效性在锁内检查。
 */

#ifndef FILESYSTEM_FILE_H
#define FILESYSTEM_FILE_H

#include <stdbool.h>
#include <stdint.h>
#include "Service/filesystem/filesystem_types.h"
#include "Service/service.h"

/** @brief 单次 ReadFile/WriteFile 的字节数上限（32 KiB，与 SD 的 DMA bounce buffer 同大）；超过返回 SERVICE_INVALID_PARAM，不截短也不在内部拆分，持锁时长因此有界。 */
#define SERVICE_FILESYSTEM_IO_MAX_BYTES (32U * 1024U)

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

/**
 * @brief 按文件定位符打开的结果；三态使“核对不符，退回路径打开”与真正的失败可区分。
 */
typedef enum
{
    SERVICE_FILESYSTEM_LOCATOR_OPENED = 0, /**< 已打开，句柄有效。 */
    SERVICE_FILESYSTEM_LOCATOR_MISMATCH,   /**< 定位符与介质不符（无定位符、条目已删除、是目录、起始簇或长度不同、位置越界）；未占用文件槽，调用者应退回按路径打开。 */
    SERVICE_FILESYSTEM_LOCATOR_FAILED      /**< 其他失败（卷未就绪、槽用尽、介质错误等）；原因见 status 输出，退回路径打开通常无益。 */
} Service_Filesystem_LocatorOpenResultTypeDef;

Service_StatusTypeDef Service_Filesystem_OpenFile(
    Service_Filesystem_VolumeTypeDef volume,
    const char *path,
    Service_Filesystem_FileModeTypeDef mode,
    Service_Filesystem_FileHandleTypeDef *file);
Service_Filesystem_LocatorOpenResultTypeDef Service_Filesystem_OpenFileByLocator(
    Service_Filesystem_VolumeTypeDef volume,
    const Service_Filesystem_FileLocatorTypeDef *locator,
    Service_Filesystem_FileHandleTypeDef *file,
    Service_StatusTypeDef *status);
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
