/**
  ******************************************************************************
  * @file    storage_flash_benchmark_config.h
  * @brief   Storage Task 外部 NOR Flash 基准的私有数据规模参数。
  *
  * @details
  *          本 Module 同时包含非破坏性的 0xEC 顺序读取测速，以及仅在显式开关
  *          开启时调用 Platform Flash 的首尾 4 KiB 保留自检扇区诊断。自检区地址
  *          由 Platform/flash 依 ADR-0009 持有，APP 不重复定义物理分区。
  ******************************************************************************
  */

#ifndef STORAGE_FLASH_BENCHMARK_CONFIG_H
#define STORAGE_FLASH_BENCHMARK_CONFIG_H

/* storage_flash_benchmark.c */
#define STORAGE_FLASH_BENCHMARK_READ_START_ADDRESS       0x00000000U    /* 顺序读取起点；须满足 0xEC 的 4-byte 首地址对齐。 */
#define STORAGE_FLASH_BENCHMARK_READ_TOTAL_BYTES         (1U * 1024U * 1024U)  /* 单次读取基准覆盖的总字节数。 */
#define STORAGE_FLASH_BENCHMARK_READ_CHUNK_BYTES         (4U * 1024U)   /* 工作缓冲固定为一块 4 KiB 扇区。 */

#define STORAGE_FLASH_BENCHMARK_MDMA_TIMEOUT_MS          100U           /* 单块 MDMA 超时；到期后仍须确认 DMA 安全收尾才能返回。 */
#define STORAGE_FLASH_BENCHMARK_PAGE_PROGRAM_TIMEOUT_MS  10U            /* 页编程超时；tPP 最大 4 ms，另留任务通知与收尾裕量。 */
#define STORAGE_FLASH_BENCHMARK_SECTOR_ERASE_TIMEOUT_MS  600U           /* 4 KiB 擦除超时；tSE 最大 400 ms，另留调度与测量裕量。 */
#define STORAGE_FLASH_BENCHMARK_PROGRAM_PAGE_BYTES       256U           /* 自检始终以 W25Qxx 固定物理 256-byte 页提交写入。 */
#define STORAGE_FLASH_BENCHMARK_PROGRAM_ENABLE           1              /* 总开关开启时，1 允许对首尾保留自检扇区擦写。 */

#if ((STORAGE_FLASH_BENCHMARK_READ_START_ADDRESS % 4U) != 0U)
#error "STORAGE_FLASH_BENCHMARK_READ_START_ADDRESS must be 4-byte aligned."
#endif
#if ((STORAGE_FLASH_BENCHMARK_READ_TOTAL_BYTES == 0U) || \
     (STORAGE_FLASH_BENCHMARK_READ_CHUNK_BYTES == 0U) || \
     ((STORAGE_FLASH_BENCHMARK_READ_TOTAL_BYTES % \
       STORAGE_FLASH_BENCHMARK_READ_CHUNK_BYTES) != 0U) || \
     ((STORAGE_FLASH_BENCHMARK_READ_CHUNK_BYTES % 4U) != 0U))
#error "Flash read benchmark size must be a non-zero multiple of a 4-byte-aligned chunk."
#endif
#if (STORAGE_FLASH_BENCHMARK_READ_CHUNK_BYTES != (4U * 1024U))
#error "Flash benchmark work buffer must remain one 4 KiB sector."
#endif
#if (STORAGE_FLASH_BENCHMARK_MDMA_TIMEOUT_MS == 0U)
#error "Flash MDMA benchmark timeout must be non-zero."
#endif
#if ((STORAGE_FLASH_BENCHMARK_PAGE_PROGRAM_TIMEOUT_MS == 0U) || \
     (STORAGE_FLASH_BENCHMARK_SECTOR_ERASE_TIMEOUT_MS == 0U) || \
     ((STORAGE_FLASH_BENCHMARK_READ_CHUNK_BYTES % \
       STORAGE_FLASH_BENCHMARK_PROGRAM_PAGE_BYTES) != 0U))
#error "Flash diagnostic timing or page geometry is invalid."
#endif

/* storage_flash_file_benchmark.c */
#define STORAGE_FLASH_BENCHMARK_FILE_ENABLE              1              /* 文件测速随 Flash 总开关执行；仅在成功挂载后运行，不自动格式化。 */
#define STORAGE_FLASH_BENCHMARK_FILE_NAME                "__ftlrw.bin"  /* 根目录专用测试文件名；同名则拒绝新建，不覆盖、不预先删除。 */
#ifndef STORAGE_FLASH_BENCHMARK_FILE_TOTAL_BYTES
#define STORAGE_FLASH_BENCHMARK_FILE_TOTAL_BYTES         (1U * 1024U * 1024U)  /* 默认 1 MiB；可由主机测试覆盖，须为分块整数倍且不超过卷可用空间。 */
#endif
#define STORAGE_FLASH_BENCHMARK_FILE_CHUNK_BYTES         (4U * 1024U)   /* 默认 4 KiB 应用缓冲，跨越七扇区组边界以覆盖 FTL 部分组更新。 */

#if ((STORAGE_FLASH_BENCHMARK_FILE_TOTAL_BYTES == 0U) || \
     (STORAGE_FLASH_BENCHMARK_FILE_CHUNK_BYTES == 0U) || \
     ((STORAGE_FLASH_BENCHMARK_FILE_TOTAL_BYTES % STORAGE_FLASH_BENCHMARK_FILE_CHUNK_BYTES) != 0U))
#error "Flash file benchmark requires non-zero, whole chunks."
#endif

#endif /* STORAGE_FLASH_BENCHMARK_CONFIG_H */
