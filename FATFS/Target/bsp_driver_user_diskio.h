/**
 * @file bsp_driver_user_diskio.h
 * @brief 项目自维护的 USER DiskIO 后端契约；不依赖 Service 或 HAL。
 */

#ifndef BSP_DRIVER_USER_DISKIO_H
#define BSP_DRIVER_USER_DISKIO_H

#include "Middlewares/Third_Party/FatFs/src/diskio.h"
DSTATUS BSP_USER_DISKIO_Init(BYTE lun);
DSTATUS BSP_USER_DISKIO_GetStatus(BYTE lun);
DRESULT BSP_USER_DISKIO_ReadBlocks(BYTE lun, BYTE *buffer, DWORD sector, UINT count);
DRESULT BSP_USER_DISKIO_WriteBlocks(BYTE lun, const BYTE *buffer, DWORD sector, UINT count);
DRESULT BSP_USER_DISKIO_Ioctl(BYTE lun, BYTE command, void *buffer);
#endif
