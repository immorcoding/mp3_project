/**
 ******************************************************************************
 * @file    resource_service.h
 * @brief   当前产品启动期只读资源加载 Service Interface。
 ******************************************************************************
 */

#ifndef RESOURCE_SERVICE_H
#define RESOURCE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "Service/service.h"

/** @brief Resource Service 初始化失败阶段。 */
typedef enum
{
    SERVICE_RESOURCE_STAGE_NONE = 0U,
    SERVICE_RESOURCE_STAGE_MAP_FLASH,
    SERVICE_RESOURCE_STAGE_OPEN_PACKAGE,
    SERVICE_RESOURCE_STAGE_CHECK_IDENTITY,
    SERVICE_RESOURCE_STAGE_FIND_ENTRY,
    SERVICE_RESOURCE_STAGE_CHECK_TYPE,
    SERVICE_RESOURCE_STAGE_CHECK_METADATA,
    SERVICE_RESOURCE_STAGE_CHECK_DESTINATION,
    SERVICE_RESOURCE_STAGE_VERIFY_DATA,
    SERVICE_RESOURCE_STAGE_CLEAN_CACHE
} Service_ResourceStageTypeDef;

/** @brief Resource Service 最近一次初始化结果快照。 */
typedef struct
{
    bool IsReady;
    uint32_t FailedResourceID;
    Service_ResourceStageTypeDef FailedStage;
    uint32_t ComponentStatus;
    uint32_t VendorID;
    uint32_t ProductID;
    uint64_t PackageVersion;
    uint16_t EntryCount;
} Service_ResourceDiagnosticsTypeDef;

Service_StatusTypeDef Service_Resource_Init(void);
bool Service_Resource_IsReady(void);
Service_StatusTypeDef Service_Resource_GetDiagnostics(
    Service_ResourceDiagnosticsTypeDef *diagnostics);

#endif /* RESOURCE_SERVICE_H */
