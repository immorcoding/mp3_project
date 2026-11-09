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
#define FILESYSTEM_SD_FILE_SLOT_COUNT        4U      /* SD 卷的文件槽数量；槽按卷固定分配，由所属卷的锁保护，用尽返回 SERVICE_BUSY，两卷槽总数静态断言不超过 `_FS_LOCK`（ADR-0024 第 3 条）。 */
#define FILESYSTEM_SD_DIRECTORY_SLOT_COUNT   2U      /* SD 卷的目录槽数量。 */
#define FILESYSTEM_FLASH_FILE_SLOT_COUNT     2U      /* Flash 卷的文件槽数量。 */
#define FILESYSTEM_FLASH_DIRECTORY_SLOT_COUNT 1U     /* Flash 卷的目录槽数量。 */
#define FILESYSTEM_FILE_SLOT_COUNT           (FILESYSTEM_SD_FILE_SLOT_COUNT + FILESYSTEM_FLASH_FILE_SLOT_COUNT)  /* 两卷文件槽总数；槽号从 0 起先 SD 后 Flash。 */
#define FILESYSTEM_DIRECTORY_SLOT_COUNT      (FILESYSTEM_SD_DIRECTORY_SLOT_COUNT + FILESYSTEM_FLASH_DIRECTORY_SLOT_COUNT)  /* 两卷目录槽总数；槽号从 0 起先 SD 后 Flash。 */

/* filesystem_directory.c */
#define FILESYSTEM_VOLUME_COUNT              2U      /* 卷个数：SD、Flash，下标就是 Service_Filesystem_VolumeTypeDef 的值；目录定位符采集的扇区缓冲按它每卷一份。 */

/* filesystem_lock.c */
#define FILESYSTEM_DENIED_CALLER_MAX         4U      /* Service_Filesystem_DenyCaller 能登记的任务数上限；登记超出时断言停机。 */
#ifndef NDEBUG
#define FILESYSTEM_LOCK_STATS_ENABLE         1U      /* 持锁统计开关：固件 Debug 开（没有 NDEBUG）；Release 带 -DNDEBUG，整段编译掉。 */
#else
#define FILESYSTEM_LOCK_STATS_ENABLE         0U      /* 持锁统计开关：Release 关。 */
#endif
#define FILESYSTEM_LOCK_STATS_PERIOD_MS      10000U  /* 持锁统计汇总日志的节拍：一次公开调用放锁后发现距上次汇总已满此时长，就投一批（每把锁一行，共 3 行）。 */
#define FILESYSTEM_LOCK_STATS_LINE_BYTES     128U    /* 汇总行容量（含结尾 '\0'），与 Service Log 单条正文容量相同；超长的文本会被 Log 丢弃，所以成行时按此截断。 */

/* filesystem_service.c */
#define FILESYSTEM_MKFS_WORK_BUFFER_SIZE     512U    /* 当前 FatFs 配置使用 512 B 扇区，格式化工作区无需占用任务栈。 */

/* filesystem_sd_transfer.c */
#define FILESYSTEM_SD_BLOCK_SIZE             512U    /* FATFS 与当前 SD Card Device 使用的单个逻辑块大小，单位为字节。 */
#define FILESYSTEM_SD_DMA_BLOCK_COUNT        64U     /* 单次 SDMMC DMA 最多合并的逻辑块数，64 块即 32 KiB 中转缓冲区。 */
#define FILESYSTEM_SD_DMA_BUFFER_ALIGNMENT   PLATFORM_DCACHE_LINE_SIZE  /* SDMMC DMA 中转对齐，复用 Platform 公开的 D-Cache 行大小。 */
#define FILESYSTEM_SD_LOG_LINE_BYTES         128U    /* 中止结果与钩子日志行的静态缓冲（含结尾 '\0'），与 Service Log 单条正文容量相同。 */

/* filesystem_sd_transfer.c：迟到 DMA 测试钩子（DIAG-7），做法与判定见 Service/filesystem/README.md */
#ifndef FILESYSTEM_HOOK_SD_LATE_DMA
#define FILESYSTEM_HOOK_SD_LATE_DMA          0U      /* 钩子开关，默认关，代码写成 `if (开关)` 而不是 `#if`；构建时 -D 或 CMake 变量 MP3_HOOK_SD_LATE_DMA=1 打开，发布构建不开。 */
#endif
#define FILESYSTEM_HOOK_SD_LATE_DMA_PERIOD   16U     /* 每多少次读请求探测一次。 */
#define FILESYSTEM_HOOK_SD_LATE_DMA_TIMEOUT_MS 1U    /* 受害读（块 0 起 32 KiB）的等待上限，单位为毫秒；32 KiB 平均要 3 ms 以上，所以必然超时并被显式中止。 */
#define FILESYSTEM_HOOK_SD_LATE_DMA_MIN_BLOCK 64U    /* 目标块起始号低于它不探测（引导区一带可能与受害块内容相同）。 */

/* filesystem_sd_bsp.c */
#define FILESYSTEM_FATFS_BSP_DMA_TIMEOUT_MS  500U    /* BSP 等一个 SDMMC DMA 分块完成事件的上限，单位为毫秒，对所有 FatFs 读写一律用它（FatFs 传来的 0xFFFFFFFF 被忽略）；依据 SD 读访问上限 100 ms、SDHC 写忙 250 ms、SDXC 写忙 500 ms 加 32 KiB 分块传输时间，小于播放侧 T_data；超时后显式中止，再返回 MSD_ERROR。 */

#endif /* FILESYSTEM_CONFIG_H */
