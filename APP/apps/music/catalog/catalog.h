/**
 * @file catalog.h
 * @brief 音乐曲库的扫描、作废、发布与读者一次读全的入口。
 * @note  公开头不暴露字符串池、条目偏移或整表指针。条数上限须能放入 uint16_t。文件定位符对调用者不透明，
 *        只在同一曲库代次内有效（ARC-19）。
 *
 *        发布规则（STOR-1、#196）：一份表，模块内一把短锁，持锁只拷内存、不调文件接口、不投日志。
 *        扫描开始时锁内把代次置 0，之后写表不拿锁，读者看到代次 0 就不碰表；发布时锁内比对卷序号，
 *        一致才写代次和条数；代次只在发布时加 1，一次开机内不重复；取代次时卷序号不符就算 0。
 *        扫描是后台任务里的单步作业 `catalog_scan_step()`（音乐注册进注册表的 `background_step`）：
 *        一步最多一次目录接口调用，读目录一步读一条；每步先比对卷序号，变了就关目录并丢弃，卡还在就重新开始。
 *        详见 `APP/apps/music/catalog/README.md`。
 */

#ifndef CATALOG_H
#define CATALOG_H

#include <stdbool.h>
#include <stdint.h>
#include "APP/apps/music/catalog/catalog_config.h"
#include "Service/filesystem/filesystem_types.h"

/**
 * @brief 读者一次读全的结果：同一把锁内拷出的代次、条数、路径和文件定位符。
 * @note  `Generation` 为 0 表示曲库此刻不可用（扫描中、卷变了、没发布），其余字段无效（`Count` 为 0，
 *        路径为空串，定位符全零）。体积约 300 B，调用者放静态存储，不放在小栈上。
 */
typedef struct
{
    uint32_t Generation;                                 /**< 曲库代次；0 表示不可用。 */
    uint16_t Count;                                      /**< 代次非 0 时的曲库条数。 */
    char Path[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];   /**< 位置有效时的 UTF-8 相对路径。 */
    Service_Filesystem_FileLocatorTypeDef Locator;       /**< 位置有效时的文件定位符，可能全零。 */
} Catalog_TrackTypeDef;

void catalog_boot_init(void);
uint32_t catalog_scan_step(void);
uint32_t catalog_generation(void);
bool catalog_read_track(uint16_t position, Catalog_TrackTypeDef *out);

#endif /* CATALOG_H */
