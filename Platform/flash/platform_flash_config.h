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

#endif /* PLATFORM_FLASH_CONFIG_H */
