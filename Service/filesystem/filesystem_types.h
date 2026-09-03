/**
 * @file filesystem_types.h
 * @brief Filesystem 公开的卷与路径契约；文件、目录和卷生命周期头均依赖它。
 */

#ifndef FILESYSTEM_TYPES_H
#define FILESYSTEM_TYPES_H

#include <stdint.h>

/** @brief 对外 UTF-8 相对路径的最大字节数，不含结尾 '\0'。 */
#define SERVICE_FILESYSTEM_PATH_MAX_BYTES 255U

/** @brief 单个目录项 UTF-8 名称的最大字节数，不含结尾 '\0'。 */
#define SERVICE_FILESYSTEM_NAME_MAX_BYTES 255U

/**
 * @brief Filesystem 对外可见的逻辑卷。
 * @note  相对路径只在所选卷内解释，不会同时搜索两个卷。
 */
typedef enum
{
    SERVICE_FILESYSTEM_VOLUME_SD = 0, /**< SD 卡上的 FatFs 逻辑卷；相对路径只在此卷内解释。 */
    SERVICE_FILESYSTEM_VOLUME_FLASH   /**< 外部 NOR Flash FTL 上的 FatFs 逻辑卷；相对路径只在此卷内解释。 */
} Service_Filesystem_VolumeTypeDef;

#endif /* FILESYSTEM_TYPES_H */
