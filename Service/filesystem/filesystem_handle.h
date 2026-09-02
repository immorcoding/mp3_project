/**
 * @file filesystem_handle.h
 * @brief Filesystem Module 私有文件/目录槽、Token 与按卷失效。
 */

#ifndef FILESYSTEM_HANDLE_H
#define FILESYSTEM_HANDLE_H

#include <stdbool.h>
#include "Middlewares/Third_Party/FatFs/src/ff.h"
#include "Service/filesystem/filesystem_directory.h"
#include "Service/filesystem/filesystem_file.h"

void filesystem_handle_set_volume_initialized(Service_Filesystem_VolumeTypeDef volume, bool ready);
void filesystem_handle_set_mounted(Service_Filesystem_VolumeTypeDef volume, bool mounted);
bool filesystem_handle_is_mounted(Service_Filesystem_VolumeTypeDef volume);
bool filesystem_handle_volume_has_open(Service_Filesystem_VolumeTypeDef volume);
Service_StatusTypeDef filesystem_handle_volume_ready(Service_Filesystem_VolumeTypeDef volume);

Service_StatusTypeDef filesystem_handle_acquire_file(Service_Filesystem_VolumeTypeDef volume,
                                                     FIL **object,
                                                     Service_Filesystem_FileHandleTypeDef *file);
Service_StatusTypeDef filesystem_handle_lookup_file(Service_Filesystem_FileHandleTypeDef file,
                                                    FIL **object);
void filesystem_handle_release_file(Service_Filesystem_FileHandleTypeDef file);

Service_StatusTypeDef filesystem_handle_acquire_directory(
    Service_Filesystem_VolumeTypeDef volume,
    DIR **object,
    Service_Filesystem_DirectoryHandleTypeDef *directory);
Service_StatusTypeDef filesystem_handle_lookup_directory(
    Service_Filesystem_DirectoryHandleTypeDef directory,
    DIR **object);
void filesystem_handle_release_directory(Service_Filesystem_DirectoryHandleTypeDef directory);

#endif /* FILESYSTEM_HANDLE_H */
