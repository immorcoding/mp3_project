/**
 ******************************************************************************
 * @file    filesystem_service.h
 * @brief   Storage Task 独占使用的 FatFs 卷生命周期 Interface。
 ******************************************************************************
 */

#ifndef FILESYSTEM_SERVICE_H
#define FILESYSTEM_SERVICE_H

#include <stdint.h>
#include "Service/service.h"

Service_StatusTypeDef Service_Filesystem_InitSD(uint32_t notify_index);
Service_StatusTypeDef Service_Filesystem_MountSD(void);
Service_StatusTypeDef Service_Filesystem_UnmountSD(void);

Service_StatusTypeDef Service_Filesystem_InitFlash(uint32_t notify_index);
Service_StatusTypeDef Service_Filesystem_MountFlash(void);
Service_StatusTypeDef Service_Filesystem_UnmountFlash(void);
Service_StatusTypeDef Service_Filesystem_FormatAndMountFlash(void);
Service_StatusTypeDef Service_Filesystem_RecoverAndMountFlash(void);
Service_StatusTypeDef Service_Filesystem_ReclaimFlash(void);

#endif /* FILESYSTEM_SERVICE_H */
