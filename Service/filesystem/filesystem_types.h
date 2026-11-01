/**
 * @file filesystem_types.h
 * @brief Filesystem 公开的卷与路径契约；文件、目录和卷生命周期头均依赖它。
 */

#ifndef FILESYSTEM_TYPES_H
#define FILESYSTEM_TYPES_H

#include <stdint.h>

#define SERVICE_FILESYSTEM_PATH_MAX_BYTES 255U /* 对外 UTF-8 相对路径最大字节数，不含结尾 '\0'。 */
#define SERVICE_FILESYSTEM_NAME_MAX_BYTES 255U /* 单个目录项 UTF-8 名称最大字节数，不含结尾 '\0'。 */

/**
 * @brief Filesystem 对外可见的逻辑卷。
 * @note  相对路径只在所选卷内解释，不会同时搜索两个卷。
 */
typedef enum
{
    SERVICE_FILESYSTEM_VOLUME_SD = 0, /**< SD 卡上的 FatFs 逻辑卷；相对路径只在此卷内解释。 */
    SERVICE_FILESYSTEM_VOLUME_FLASH   /**< 外部 NOR Flash FTL 上的 FatFs 逻辑卷；相对路径只在此卷内解释。 */
} Service_Filesystem_VolumeTypeDef;

/**
 * @brief 不透明文件定位符；目录扫描时随条目给出，之后可不查名称直接打开该文件。
 * @note  内容归 Filesystem Service 私有（文件夹起始簇、主目录项字节偏移、文件起始簇、文件长度，共 16 B），
 *        调用者只能整体保存、拷贝和传回，不得解释、修改，也不得跨卷或跨曲库代次使用。
 *        全零表示“无定位符”：按它打开一律返回核对不符，由调用者退回按路径打开。
 */
typedef struct
{
    uint32_t Opaque[4]; /**< Service 私有内容，调用者不得解释。 */
} Service_Filesystem_FileLocatorTypeDef;

#endif /* FILESYSTEM_TYPES_H */
