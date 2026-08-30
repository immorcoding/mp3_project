/**
  ******************************************************************************
  * @file    platform_flash_config.h
  * @brief   当前 PCB W25Q256 QSPI 装配的私有参数。
  ******************************************************************************
  */

#ifndef PLATFORM_FLASH_CONFIG_H
#define PLATFORM_FLASH_CONFIG_H

#include "Components/w25qxx/w25qxx.h"

/* JEDEC ID 是短同步事务；本值属于当前 HAL QSPI 后端的板级超时策略。 */
#define PLATFORM_FLASH_QSPI_TIMEOUT_MS        100u

/* 当前 PCB 焊接 W25Q256；MemoryType 不属于板级兼容性判定条件。 */
#define PLATFORM_FLASH_EXPECTED_MANUFACTURER_ID \
    W25QXX_MANUFACTURER_ID_WINBOND
#define PLATFORM_FLASH_EXPECTED_CAPACITY_ID     \
    W25QXX_CAPACITY_ID_256MBIT

/* ADR-0009 保留的低、高地址 4 KiB 破坏性自检扇区；不得交给 FTL 或资源包。 */
#define PLATFORM_FLASH_DIAGNOSTIC_HEAD_SECTOR_ADDRESS  0x00000000u
#define PLATFORM_FLASH_DIAGNOSTIC_TAIL_SECTOR_ADDRESS  0x01FFF000u
#define PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES    \
    W25QXX_SECTOR_ERASE_SIZE_BYTES

/* 两个扇区使用不同的地址相关图样，避免相同低位地址产生相同数据。 */
#define PLATFORM_FLASH_DIAGNOSTIC_HEAD_PATTERN_SEED  0x13579BDFu
#define PLATFORM_FLASH_DIAGNOSTIC_TAIL_PATTERN_SEED  0x2468ACE0u

#endif /* PLATFORM_FLASH_CONFIG_H */
