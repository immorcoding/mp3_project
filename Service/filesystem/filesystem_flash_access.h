/**
 * @file filesystem_flash_access.h
 * @brief Storage Task 启动诊断使用的 Flash 原始访问 Interface。
 */

#ifndef FILESYSTEM_FLASH_ACCESS_H
#define FILESYSTEM_FLASH_ACCESS_H

#include <stdint.h>
#include "Platform/flash/platform_flash.h"
#include "Service/service.h"

Service_StatusTypeDef Service_Filesystem_ReadFlashArray(uint32_t address,
                                                       uint8_t *data,
                                                       uint32_t data_length,
                                                       uint32_t timeout_ms);
Service_StatusTypeDef Service_Filesystem_ReadFlashDiagnostic(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint8_t *data,
    uint32_t data_length,
    uint32_t timeout_ms);
Service_StatusTypeDef Service_Filesystem_EraseFlashDiagnostic(
    Platform_Flash_DiagnosticRegionTypeDef region, uint32_t timeout_ms);
Service_StatusTypeDef Service_Filesystem_ProgramFlashDiagnostic(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint32_t page_offset,
    const uint8_t *data,
    uint32_t data_length,
    uint32_t timeout_ms);

#endif /* FILESYSTEM_FLASH_ACCESS_H */
