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
