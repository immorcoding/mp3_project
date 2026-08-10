/**
  ******************************************************************************
  * @file    filesystem_service.h
  * @brief   Storage Task 独占使用的 FatFs 卷操作 Interface。
  ******************************************************************************
  */

#ifndef FILESYSTEM_SERVICE_H
#define FILESYSTEM_SERVICE_H

#include "Middlewares/Third_Party/FatFs/src/ff.h"

FRESULT Filesystem_Init(void);
FRESULT Filesystem_FormatSD(void);
FRESULT Filesystem_MountSD(void);
FRESULT Filesystem_UnmountSD(void);

#endif /* FILESYSTEM_SERVICE_H */
