/**
 ******************************************************************************
 * @file    resource_service_config.h
 * @brief   当前产品 RPKC1 来源、身份、资源 ID 和日志配置。
 ******************************************************************************
 */

#ifndef RESOURCE_SERVICE_CONFIG_H
#define RESOURCE_SERVICE_CONFIG_H

#define SERVICE_RESOURCE_FLASH_OFFSET           0x00401000UL
#define SERVICE_RESOURCE_MAXIMUM_SIZE           0x003FE000UL
#define SERVICE_RESOURCE_VENDOR_ID              1UL
#define SERVICE_RESOURCE_PRODUCT_ID             1UL

#define SERVICE_RESOURCE_ID_CP936_UNI2OEM       1UL
#define SERVICE_RESOURCE_ID_CP936_OEM2UNI       2UL
#define SERVICE_RESOURCE_ID_DEFAULT_WALLPAPER   3UL

#define SERVICE_RESOURCE_CP936_TABLE_BYTES      0x00015484UL
#define SERVICE_RESOURCE_CP936_ELEMENT_COUNT    43586UL
#define SERVICE_RESOURCE_WALLPAPER_BYTES         0x00038400UL
#define SERVICE_RESOURCE_WALLPAPER_WIDTH         240UL
#define SERVICE_RESOURCE_WALLPAPER_HEIGHT        320UL
#define SERVICE_RESOURCE_WALLPAPER_STRIDE_BYTES  720UL

#define SERVICE_RESOURCE_LOG_ENABLE              1
#define SERVICE_RESOURCE_LOG_ENTRY_ENABLE        1
#define SERVICE_RESOURCE_LOG_TIMING_ENABLE       1

#endif /* RESOURCE_SERVICE_CONFIG_H */
