/**
 * @file catalog_track_open.h
 * @brief 曲目文件的打开：先按曲库里的文件定位符打开，核对不符退回按路径打开（STOR-17、ARC-19、DIAG-5）。
 * @note  曲名、Queue 窗口行（前台任务）、开启音频流与跳转重开流（音频读取任务）四条路径共用这一个入口。
 */

#ifndef CATALOG_TRACK_OPEN_H
#define CATALOG_TRACK_OPEN_H

#include <stdint.h>

#include "catalog.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/service.h"

Service_StatusTypeDef catalog_track_open(uint16_t position,
                                         const Catalog_TrackTypeDef *track,
                                         Service_Filesystem_FileHandleTypeDef *file,
                                         uint32_t *open_us);

#endif /* CATALOG_TRACK_OPEN_H */
