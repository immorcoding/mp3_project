/**
 ******************************************************************************
 * @file    filesystem_service.h
 * @brief   Storage Task 独占使用的 FatFs 卷操作 Interface。
 ******************************************************************************
 */

#ifndef FILESYSTEM_SERVICE_H
#define FILESYSTEM_SERVICE_H

#include "Service/service.h"
#include <stdbool.h>
#include <stdint.h>
#include "Platform/flash/platform_flash.h"

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

Service_StatusTypeDef Service_Filesystem_Init(void);
Service_StatusTypeDef Service_Filesystem_FormatSD(void);
Service_StatusTypeDef Service_Filesystem_MountSD(void);
Service_StatusTypeDef Service_Filesystem_UnmountSD(void);

Service_StatusTypeDef Service_Filesystem_InitFlash(void);
Service_StatusTypeDef Service_Filesystem_OpenFlash(void);
Service_StatusTypeDef Service_Filesystem_MountFlash(void);
Service_StatusTypeDef Service_Filesystem_UnmountFlash(void);
Service_StatusTypeDef Service_Filesystem_FormatFlash(void);
Service_StatusTypeDef Service_Filesystem_MaintainFlash(void);
Service_StatusTypeDef Service_Filesystem_RecoverFlash(void);
bool Service_Filesystem_ReadFlashArray(uint32_t address,
                                       uint8_t *data,
                                       uint32_t data_length,
                                       uint32_t timeout_ms);
bool Service_Filesystem_ReadFlashDiagnostic(Platform_Flash_DiagnosticRegionTypeDef region,
                                            uint8_t *data,
                                            uint32_t data_length,
                                            uint32_t timeout_ms);
bool Service_Filesystem_EraseFlashDiagnostic(Platform_Flash_DiagnosticRegionTypeDef region,
                                             uint32_t timeout_ms);
bool Service_Filesystem_ProgramFlashDiagnostic(Platform_Flash_DiagnosticRegionTypeDef region,
                                               uint32_t page_offset,
                                               const uint8_t *data,
                                               uint32_t data_length,
                                               uint32_t timeout_ms);

#endif /* FILESYSTEM_SERVICE_H */
