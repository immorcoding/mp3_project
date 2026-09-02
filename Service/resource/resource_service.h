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
    SERVICE_RESOURCE_STAGE_NONE = 0U,          /**< 尚未失败，或尚未开始加载。 */
    SERVICE_RESOURCE_STAGE_MAP_FLASH,          /**< 开启外部 Flash 映射窗口失败。 */
    SERVICE_RESOURCE_STAGE_OPEN_PACKAGE,       /**< 打开 RPKC1 资源包失败。 */
    SERVICE_RESOURCE_STAGE_CHECK_IDENTITY,     /**< 资源包身份或版本与产品期望不符。 */
    SERVICE_RESOURCE_STAGE_FIND_ENTRY,         /**< 找不到当前必需的资源目录项。 */
    SERVICE_RESOURCE_STAGE_CHECK_TYPE,         /**< 资源类型不是当前加载路径所接受的类型。 */
    SERVICE_RESOURCE_STAGE_CHECK_METADATA,     /**< 资源元数据非法或不被支持。 */
    SERVICE_RESOURCE_STAGE_CHECK_DESTINATION,  /**< 加载目标地址、容量或对齐不满足要求。 */
    SERVICE_RESOURCE_STAGE_VERIFY_DATA,        /**< 资源载荷校验失败。 */
    SERVICE_RESOURCE_STAGE_CLEAN_CACHE         /**< 映射窗口的 Cache 维护失败。 */
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
