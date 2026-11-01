/**
 * @file filesystem_locator.h
 * @brief Filesystem Module 私有：文件定位符的字段布局与打包/解包。
 *
 * @details
 *          定位符内容为 4 个 32 位字：文件夹起始簇（根目录为 0）、主目录项在文件夹内的字节偏移
 *          （这两项定位），文件起始簇与文件长度（这两项核对）。文件起始簇为 0 表示无定位符
 *          （空文件没有簇，也无需靠定位符加速）。布局只在本 Module 内有意义，公开类型对调用者不透明。
 */

#ifndef FILESYSTEM_LOCATOR_H
#define FILESYSTEM_LOCATOR_H

#include <stdbool.h>
#include <stdint.h>
#include "Service/filesystem/filesystem_types.h"

#define FILESYSTEM_LOCATOR_WORD_FOLDER 0U /* Opaque[] 下标：文件夹起始簇。 */
#define FILESYSTEM_LOCATOR_WORD_OFFSET 1U /* Opaque[] 下标：主目录项在文件夹内的字节偏移。 */
#define FILESYSTEM_LOCATOR_WORD_START  2U /* Opaque[] 下标：文件起始簇，0 表示无定位符。 */
#define FILESYSTEM_LOCATOR_WORD_LENGTH 3U /* Opaque[] 下标：文件长度。 */

/**
 * @brief 打包一个定位符。
 * @param[in] folder_cluster 文件夹起始簇，根目录为 0。
 * @param[in] entry_offset 主目录项字节偏移。
 * @param[in] start_cluster 文件起始簇；为 0 时结果是“无定位符”。
 * @param[in] length 文件长度。
 * @return 定位符值。
 */
static inline Service_Filesystem_FileLocatorTypeDef filesystem_locator_make(uint32_t folder_cluster,
                                                                            uint32_t entry_offset,
                                                                            uint32_t start_cluster,
                                                                            uint32_t length)
{
    Service_Filesystem_FileLocatorTypeDef locator = {{0U, 0U, 0U, 0U}};

    if (start_cluster != 0U)
    {
        locator.Opaque[FILESYSTEM_LOCATOR_WORD_FOLDER] = folder_cluster;
        locator.Opaque[FILESYSTEM_LOCATOR_WORD_OFFSET] = entry_offset;
        locator.Opaque[FILESYSTEM_LOCATOR_WORD_START] = start_cluster;
        locator.Opaque[FILESYSTEM_LOCATOR_WORD_LENGTH] = length;
    }
    return locator;
}

/**
 * @brief 判断定位符是否带有内容。
 * @param[in] locator 待检查的定位符，不得为空。
 * @return true 起始簇非零，可以尝试按它打开。
 */
static inline bool filesystem_locator_is_set(const Service_Filesystem_FileLocatorTypeDef *locator)
{
    return locator->Opaque[FILESYSTEM_LOCATOR_WORD_START] != 0U;
}

#endif /* FILESYSTEM_LOCATOR_H */
