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

/* 读取基准必须满足当前 W25Q256 0xEC 的 4-byte 首地址对齐约束。 */
#define STORAGE_FLASH_BENCHMARK_READ_START_ADDRESS     0x00000000U
#define STORAGE_FLASH_BENCHMARK_READ_TOTAL_BYTES       (1U * 1024U * 1024U)
#define STORAGE_FLASH_BENCHMARK_READ_CHUNK_BYTES       (4U * 1024U)

/* 单块 MDMA 的超时预算；到期后 Service 仍须确认 DMA 安全收尾才能返回。 */
#define STORAGE_FLASH_BENCHMARK_MDMA_TIMEOUT_MS        100U

/* W25Q256JV tPP 最大 4 ms；为任务通知、Status Match 与普通上下文收尾留出裕量。 */
#define STORAGE_FLASH_BENCHMARK_PAGE_PROGRAM_TIMEOUT_MS 10U

/* W25Q256JV 4 KiB tSE 最大 400 ms；为任务调度和时钟测量留出裕量。 */
#define STORAGE_FLASH_BENCHMARK_SECTOR_ERASE_TIMEOUT_MS 600U

/* 自检始终以 W25Qxx 固定物理 256-byte 页为单位提交写入。 */
#define STORAGE_FLASH_BENCHMARK_PROGRAM_PAGE_BYTES      256U

/* 总开关开启时，1 允许对首尾保留自检扇区擦写；0 关闭这项破坏性自检。 */
#define STORAGE_FLASH_BENCHMARK_PROGRAM_ENABLE         1

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


/** @brief 文件测速随 Flash 总开关执行；仅在成功挂载后运行，不自动格式化。 */
#define STORAGE_FLASH_BENCHMARK_FILE_ENABLE 1

/** @brief 根目录专用测试文件名；同名文件存在则拒绝新建，不覆盖、不预先删除。 */
#define STORAGE_FLASH_BENCHMARK_FILE_NAME "__ftlrw.bin"

/** @brief 默认 1 MiB；可由主机测试覆盖，须为分块的整数倍且不超过卷可用空间。 */
#ifndef STORAGE_FLASH_BENCHMARK_FILE_TOTAL_BYTES
#define STORAGE_FLASH_BENCHMARK_FILE_TOTAL_BYTES (1U * 1024U * 1024U)
#endif

/** @brief 默认 4 KiB 应用缓冲，跨越七扇区组边界以覆盖 FTL 部分组更新。 */
#define STORAGE_FLASH_BENCHMARK_FILE_CHUNK_BYTES (4U * 1024U)

#if ((STORAGE_FLASH_BENCHMARK_FILE_TOTAL_BYTES == 0U) || \
     (STORAGE_FLASH_BENCHMARK_FILE_CHUNK_BYTES == 0U) || \
     ((STORAGE_FLASH_BENCHMARK_FILE_TOTAL_BYTES % STORAGE_FLASH_BENCHMARK_FILE_CHUNK_BYTES) != 0U))
#error "Flash file benchmark requires non-zero, whole chunks."
#endif

#endif /* STORAGE_FLASH_BENCHMARK_CONFIG_H */
