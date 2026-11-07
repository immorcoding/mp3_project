/**
 ******************************************************************************
 * @file    filesystem_service.h
 * @brief   FatFs 卷生命周期 Interface 与调用者限制；线程安全（每卷一把锁）。
 ******************************************************************************
 */

#ifndef FILESYSTEM_SERVICE_H
#define FILESYSTEM_SERVICE_H

#include <stdint.h>
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Service/service.h"

Service_StatusTypeDef Service_Filesystem_InitSD(void);
Service_StatusTypeDef Service_Filesystem_MountSD(void);
Service_StatusTypeDef Service_Filesystem_UnmountSD(void);

Service_StatusTypeDef Service_Filesystem_InitFlash(void);
Service_StatusTypeDef Service_Filesystem_MountFlash(void);
Service_StatusTypeDef Service_Filesystem_UnmountFlash(void);
Service_StatusTypeDef Service_Filesystem_FormatAndMountFlash(void);
Service_StatusTypeDef Service_Filesystem_RecoverAndMountFlash(void);
Service_StatusTypeDef Service_Filesystem_ReclaimFlash(void);

void Service_Filesystem_DenyCaller(TaskHandle_t task);

#endif /* FILESYSTEM_SERVICE_H */
