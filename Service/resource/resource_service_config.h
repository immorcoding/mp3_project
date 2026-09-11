/**
  ******************************************************************************
  * @file    resource_service_config.h
  * @brief   当前产品 RPKC1 来源、身份、资源 ID 和日志配置。
  ******************************************************************************
  */

#ifndef RESOURCE_SERVICE_CONFIG_H
#define RESOURCE_SERVICE_CONFIG_H

/* resource_service.c */
#define SERVICE_RESOURCE_FLASH_OFFSET            0x00401000UL  /* 资源包在外部 Flash 中的起始物理偏移。 */
#define SERVICE_RESOURCE_MAXIMUM_SIZE            0x003FE000UL  /* 允许打开的资源包最大字节数。 */
#define SERVICE_RESOURCE_VENDOR_ID               1UL           /* 当前产品接受的资源包厂商 ID。 */
#define SERVICE_RESOURCE_PRODUCT_ID              1UL           /* 当前产品接受的资源包产品 ID。 */

#define SERVICE_RESOURCE_ID_CP936_UNI2OEM        1UL           /* Unicode 到 CP936 的转码表资源 ID。 */
#define SERVICE_RESOURCE_ID_CP936_OEM2UNI        2UL           /* CP936 到 Unicode 的转码表资源 ID。 */
#define SERVICE_RESOURCE_ID_DEFAULT_WALLPAPER    3UL           /* 默认壁纸资源 ID。 */
#define SERVICE_RESOURCE_ID_VINYL_DISC           4UL           /* Now Playing 唱盘底图资源 ID。 */

#define SERVICE_RESOURCE_CP936_TABLE_BYTES       0x00015484UL  /* CP936 转码表载荷字节数。 */
#define SERVICE_RESOURCE_CP936_ELEMENT_COUNT     43586UL       /* CP936 转码表元素个数。 */
#define SERVICE_RESOURCE_WALLPAPER_BYTES         0x00038400UL  /* 默认壁纸载荷字节数。 */
#define SERVICE_RESOURCE_WALLPAPER_WIDTH         240UL         /* 默认壁纸宽度，单位为像素。 */
#define SERVICE_RESOURCE_WALLPAPER_HEIGHT        320UL         /* 默认壁纸高度，单位为像素。 */
#define SERVICE_RESOURCE_WALLPAPER_STRIDE_BYTES  720UL         /* 默认壁纸一行字节数。 */

#define SERVICE_RESOURCE_VINYL_BYTES             0x0000F300UL  /* 唱盘底图载荷字节数。 */
#define SERVICE_RESOURCE_VINYL_WIDTH             144UL         /* 唱盘底图宽度，单位为像素。 */
#define SERVICE_RESOURCE_VINYL_HEIGHT            144UL         /* 唱盘底图高度，单位为像素。 */
#define SERVICE_RESOURCE_VINYL_STRIDE_BYTES      432UL         /* 唱盘底图一行字节数。 */

#define SERVICE_RESOURCE_LOG_ENABLE              1             /* 打开资源加载摘要日志。 */
#define SERVICE_RESOURCE_LOG_ENTRY_ENABLE        1             /* 打开单条资源校验过程日志。 */
#define SERVICE_RESOURCE_LOG_TIMING_ENABLE       1             /* 打开资源加载耗时日志。 */

#endif /* RESOURCE_SERVICE_CONFIG_H */
