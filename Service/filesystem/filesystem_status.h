/**
 * @file filesystem_status.h
 * @brief Filesystem Module 私有的 FRESULT 映射与盘符路径转换。
 */

#ifndef FILESYSTEM_STATUS_H
#define FILESYSTEM_STATUS_H

#include <stdbool.h>
#include "Middlewares/Third_Party/FatFs/src/ff.h"
#include "Service/service.h"

Service_StatusTypeDef filesystem_make_service_status(FRESULT result);
bool filesystem_make_drive_path(const char *source, TCHAR *destination);

#endif /* FILESYSTEM_STATUS_H */
