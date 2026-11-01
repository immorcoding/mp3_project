/**
 * @file storage_track_open.h
 * @brief 曲目文件的打开：先按曲库里的文件定位符打开，核对不符退回按路径打开（STOR-17、ARC-19、DIAG-5）。
 * @note  名称槽、Queue 窗口行、开启音频流与跳转重开流四条路径共用这一个入口。只由 Storage Task 调用。
 */

#ifndef STORAGE_TRACK_OPEN_H
#define STORAGE_TRACK_OPEN_H

#include <stdint.h>

#include "Service/filesystem/filesystem_file.h"
#include "Service/service.h"

Service_StatusTypeDef storage_track_open(uint16_t position,
                                         uint16_t catalog_index,
                                         const char *path,
                                         Service_Filesystem_FileHandleTypeDef *file,
                                         uint32_t *open_us);

#endif /* STORAGE_TRACK_OPEN_H */
