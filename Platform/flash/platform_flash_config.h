/**
  ******************************************************************************
  * @file    platform_flash_config.h
  * @brief   当前 PCB W25Q256 QSPI 装配的私有参数。
  ******************************************************************************
  */

#ifndef PLATFORM_FLASH_CONFIG_H
#define PLATFORM_FLASH_CONFIG_H

#include "Components/w25qxx/w25qxx.h"

/* platform_flash.c */
#define PLATFORM_FLASH_QSPI_TIMEOUT_MS                 100u          /* JEDEC ID 等短同步事务的板级超时，单位为毫秒。 */
#define PLATFORM_FLASH_EXPECTED_MANUFACTURER_ID        W25QXX_MANUFACTURER_ID_WINBOND  /* 当前 PCB 焊接 Winbond；MemoryType 不参与兼容性判定。 */
#define PLATFORM_FLASH_EXPECTED_CAPACITY_ID            W25QXX_CAPACITY_ID_256MBIT  /* 当前 PCB 焊接 W25Q256。 */

#define PLATFORM_FLASH_DIAGNOSTIC_HEAD_SECTOR_ADDRESS  0x00000000u   /* ADR-0009 低地址 4 KiB 破坏性自检扇区；不得交给 FTL 或资源包。 */
#define PLATFORM_FLASH_DIAGNOSTIC_TAIL_SECTOR_ADDRESS  0x01FFF000u   /* ADR-0009 高地址 4 KiB 破坏性自检扇区；不得交给 FTL 或资源包。 */
#define PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES    W25QXX_SECTOR_ERASE_SIZE_BYTES  /* 自检读写缓冲字节数，等于一个 4 KiB 扇区。 */
#define PLATFORM_FLASH_DIAGNOSTIC_HEAD_PATTERN_SEED    0x13579BDFu   /* 低地址自检图样种子，避免与高地址产生相同低位数据。 */
#define PLATFORM_FLASH_DIAGNOSTIC_TAIL_PATTERN_SEED    0x2468ACE0u   /* 高地址自检图样种子，避免与低地址产生相同低位数据。 */

#define PLATFORM_FLASH_MEMORY_MAPPED_BASE_ADDRESS      0x90000000UL  /* STM32H743 QUADSPI AHB 窗口基址。 */
#define PLATFORM_FLASH_MEMORY_MAPPED_SIZE_BYTES        (32UL * 1024UL * 1024UL)  /* 当前 W25Q256 可安全访问的映射窗口字节数。 */

#define PLATFORM_FLASH_FTL_SIZE_BYTES                  (24UL * 1024UL * 1024UL)  /* FTL 物理范围：尾自检区前向低地址预留，4 KiB 对齐。 */
#define PLATFORM_FLASH_FTL_BASE_ADDRESS                (PLATFORM_FLASH_DIAGNOSTIC_TAIL_SECTOR_ADDRESS - PLATFORM_FLASH_FTL_SIZE_BYTES)  /* FTL 分区起始地址，由尾自检区向前推算。 */
#define PLATFORM_FLASH_FTL_MIN_ADDRESS                 0x00401000UL  /* ADR-0009 两镜像槽之后的最低允许地址；更低范围不参与 FTL 擦写。 */
#define PLATFORM_FLASH_FTL_DATA_BLOCKS                 (PLATFORM_FLASH_FTL_SIZE_BYTES / 4096UL - 2UL)  /* 扣除两卷头后的 FTL 数据块数。 */

#if (PLATFORM_FLASH_FTL_SIZE_BYTES % 4096UL) || \
    (PLATFORM_FLASH_FTL_SIZE_BYTES < 128UL * 1024UL) || \
    (PLATFORM_FLASH_FTL_SIZE_BYTES > \
     PLATFORM_FLASH_DIAGNOSTIC_TAIL_SECTOR_ADDRESS - PLATFORM_FLASH_FTL_MIN_ADDRESS)
#error "FTL partition violates reserved regions or erase alignment"
#endif

#endif /* PLATFORM_FLASH_CONFIG_H */
