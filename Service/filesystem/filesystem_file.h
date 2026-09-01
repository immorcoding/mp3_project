/**
 * @file filesystem_file.h
 * @brief Storage Task 独占的 Flash 文件访问 Interface。
 */

#ifndef FILESYSTEM_FILE_H
#define FILESYSTEM_FILE_H

#include <stdint.h>
#include "Service/service.h"

/** @brief 首版文件接口只接受 Flash 根目录的 ASCII 文件名，不含卷号或路径分隔符。 */
#define SERVICE_FILESYSTEM_FILE_NAME_MAX_BYTES 31U

/** @brief Service 持有实际文件对象；Token 仅用于校验访问代次，调用者不得自行构造。 */
typedef struct
{
    uint32_t Token; /**< 零表示无效；关闭或卷注销后旧值不得继续使用。 */
} Service_Filesystem_FileTypeDef;

/** @brief 首版文件打开方式；不提供覆盖已有文件的隐式截断。 */
typedef enum
{
    SERVICE_FILESYSTEM_FILE_READ = 0, /**< 打开已有文件，只读。 */
    SERVICE_FILESYSTEM_FILE_CREATE_NEW /**< 新建可写文件；同名文件存在则拒绝。 */
} Service_Filesystem_FileModeTypeDef;

Service_StatusTypeDef Service_Filesystem_OpenFlashFile(
    const char *name,
    Service_Filesystem_FileModeTypeDef mode,
    Service_Filesystem_FileTypeDef *file);
Service_StatusTypeDef Service_Filesystem_ReadFile(
    Service_Filesystem_FileTypeDef file, void *data, uint32_t length, uint32_t *transferred);
Service_StatusTypeDef Service_Filesystem_WriteFile(
    Service_Filesystem_FileTypeDef file, const void *data, uint32_t length, uint32_t *transferred);
Service_StatusTypeDef Service_Filesystem_SyncFile(Service_Filesystem_FileTypeDef file);
Service_StatusTypeDef Service_Filesystem_CloseFile(Service_Filesystem_FileTypeDef *file);
Service_StatusTypeDef Service_Filesystem_RemoveFlashFile(const char *name);

#endif /* FILESYSTEM_FILE_H */
