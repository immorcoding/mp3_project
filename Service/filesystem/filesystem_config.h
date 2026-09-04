/**
  ******************************************************************************
  * @file    filesystem_config.h
  * @brief   Filesystem Service 的私有卷与 DMA 执行器参数。
  *
  * @details
  *          本文件统一保存当前 FatFs 卷路径长度、静态文件/目录槽、格式化工作区和同步 DMA
  *          中转参数。它仅供 Filesystem Service 的 Implementation 使用；
  *          上层继续只通过公开头发起卷、文件或目录操作。
  ******************************************************************************
  */

#ifndef FILESYSTEM_CONFIG_H
#define FILESYSTEM_CONFIG_H

#include "Platform/sd/platform_sd.h"
#include "Service/filesystem/filesystem_types.h"

/* filesystem_path.h */
#define FILESYSTEM_TCHAR_PATH_LENGTH         (SERVICE_FILESYSTEM_PATH_MAX_BYTES + 4U)  /* 含盘符前缀的 TCHAR 路径缓冲。 */
#define FILESYSTEM_PATH_MAX_BYTES            SERVICE_FILESYSTEM_PATH_MAX_BYTES  /* 公开相对路径上限，与 filesystem_types.h 保持一致。 */

/* filesystem_path.c */
#define FILESYSTEM_DRIVE_PATH_LENGTH         4U      /* CubeMX 生成的逻辑卷路径固定为 "N:/" 加结尾 '\0'。 */

/* filesystem_handle.c */
#define FILESYSTEM_FILE_SLOT_COUNT           4U      /* 静态文件槽数量；用尽后 OpenFile 返回 SERVICE_BUSY。 */
#define FILESYSTEM_DIRECTORY_SLOT_COUNT      4U      /* 静态目录槽数量；用尽后 OpenDirectory 返回 SERVICE_BUSY。 */

/* filesystem_service.c */
#define FILESYSTEM_MKFS_WORK_BUFFER_SIZE     512U    /* 当前 FatFs 配置使用 512 B 扇区，格式化工作区无需占用任务栈。 */

/* filesystem_sd_transfer.c */
#define FILESYSTEM_SD_BLOCK_SIZE             512U    /* FATFS 与当前 SD Card Device 使用的单个逻辑块大小，单位为字节。 */
#define FILESYSTEM_SD_DMA_BLOCK_COUNT        64U     /* 单次 SDMMC DMA 最多合并的逻辑块数，64 块即 32 KiB 中转缓冲区。 */
#define FILESYSTEM_SD_DMA_BUFFER_ALIGNMENT   PLATFORM_DMA_BUFFER_ALIGNMENT  /* CPU/DMA 共享缓冲区对齐要求，复用 Platform SD 的公开 Cache 约束。 */

/* filesystem_sd_bsp.c */
#define FILESYSTEM_FATFS_BSP_DMA_TIMEOUT_MS  30000U  /* 同步 FatFs DiskIO Bridge 等待一次 SDMMC DMA 事件的最大时间，单位为毫秒。 */

#endif /* FILESYSTEM_CONFIG_H */
