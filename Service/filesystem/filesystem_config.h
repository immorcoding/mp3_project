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

/* filesystem_handle.c：槽按卷固定分配，每个槽由所属卷的锁保护，不需要第三把锁（ADR-0024 第 3 条）。
 * 用尽后打开返回 SERVICE_BUSY。文件槽与目录槽的总数不得超过 FatFs 锁表 `_FS_LOCK`，这样锁表不会先满
 * （filesystem_handle.c 有静态断言）。 */
#define FILESYSTEM_SD_FILE_SLOT_COUNT        4U      /* SD 卷的文件槽数量。 */
#define FILESYSTEM_SD_DIRECTORY_SLOT_COUNT   2U      /* SD 卷的目录槽数量。 */
#define FILESYSTEM_FLASH_FILE_SLOT_COUNT     2U      /* Flash 卷的文件槽数量。 */
#define FILESYSTEM_FLASH_DIRECTORY_SLOT_COUNT 1U     /* Flash 卷的目录槽数量。 */
#define FILESYSTEM_FILE_SLOT_COUNT           (FILESYSTEM_SD_FILE_SLOT_COUNT + FILESYSTEM_FLASH_FILE_SLOT_COUNT)  /* 两卷文件槽总数；槽号从 0 起先 SD 后 Flash。 */
#define FILESYSTEM_DIRECTORY_SLOT_COUNT      (FILESYSTEM_SD_DIRECTORY_SLOT_COUNT + FILESYSTEM_FLASH_DIRECTORY_SLOT_COUNT)  /* 两卷目录槽总数；槽号从 0 起先 SD 后 Flash。 */

/* filesystem_lock.c */
#define FILESYSTEM_DENIED_CALLER_MAX         4U      /* Service_Filesystem_DenyCaller 能登记的任务数上限；登记超出时断言停机。 */
#ifndef NDEBUG
#define FILESYSTEM_LOCK_STATS_ENABLE         1U      /* 持锁统计开关：固件 Debug 开（没有 NDEBUG）；Release 带 -DNDEBUG，整段编译掉。 */
#else
#define FILESYSTEM_LOCK_STATS_ENABLE         0U
#endif
#define FILESYSTEM_LOCK_STATS_PERIOD_MS      10000U  /* 持锁统计汇总日志的节拍：一次公开调用放锁后发现距上次汇总已满此时长，就投一批（每把锁一行，共 3 行）。 */
#define FILESYSTEM_LOCK_STATS_LINE_BYTES     128U    /* 汇总行容量（含结尾 '\0'），与 Service Log 单条正文容量相同；超长的文本会被 Log 丢弃，所以成行时按此截断。 */

/* filesystem_service.c */
#define FILESYSTEM_MKFS_WORK_BUFFER_SIZE     512U    /* 当前 FatFs 配置使用 512 B 扇区，格式化工作区无需占用任务栈。 */

/* filesystem_sd_transfer.c */
#define FILESYSTEM_SD_BLOCK_SIZE             512U    /* FATFS 与当前 SD Card Device 使用的单个逻辑块大小，单位为字节。 */
#define FILESYSTEM_SD_DMA_BLOCK_COUNT        64U     /* 单次 SDMMC DMA 最多合并的逻辑块数，64 块即 32 KiB 中转缓冲区。 */
#define FILESYSTEM_SD_DMA_BUFFER_ALIGNMENT   PLATFORM_DCACHE_LINE_SIZE  /* SDMMC DMA 中转对齐，复用 Platform 公开的 D-Cache 行大小。 */

/* filesystem_sd_bsp.c */
/* 依据：SD 规范读访问上限 100 ms、SDHC 写忙 250 ms、SDXC 写忙 500 ms，另含 32 KiB 分块的传输时间；
 * 远小于播放侧 T_data（2 s）。按每个 DMA 分块计，不是整次 FatFs 请求；产品路径不写 SD，故不拆读写常量。
 * 超时后 filesystem_sd_transfer_finish 以 ERROR 收尾，BSP 返回 MSD_ERROR，Storage 回到主循环。 */
#define FILESYSTEM_FATFS_BSP_DMA_TIMEOUT_MS  500U    /* 同步 FatFs DiskIO Bridge 等待一个 SDMMC DMA 分块事件的最大时间，单位为毫秒。 */

#endif /* FILESYSTEM_CONFIG_H */
