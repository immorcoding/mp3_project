/**
 ******************************************************************************
 * @file    filesystem_service.h
 * @brief   Storage Task 独占使用的 FatFs 卷操作 Interface。
 ******************************************************************************
 */

#ifndef FILESYSTEM_SERVICE_H
#define FILESYSTEM_SERVICE_H

#include "Service/service.h"

Service_StatusTypeDef Service_Filesystem_InitSD(void);
Service_StatusTypeDef Service_Filesystem_FormatSD(void);
Service_StatusTypeDef Service_Filesystem_MountSD(void);
Service_StatusTypeDef Service_Filesystem_UnmountSD(void);

Service_StatusTypeDef Service_Filesystem_InitFlash(void);
Service_StatusTypeDef Service_Filesystem_OpenFlash(void);
Service_StatusTypeDef Service_Filesystem_MountFlash(void);
Service_StatusTypeDef Service_Filesystem_UnmountFlash(void);
Service_StatusTypeDef Service_Filesystem_FormatFlash(void);
Service_StatusTypeDef Service_Filesystem_ReclaimFlash(void);
Service_StatusTypeDef Service_Filesystem_RecoverFlash(void);

#endif /* FILESYSTEM_SERVICE_H */
