/**
  ******************************************************************************
  * @file    platform_flash.h
  * @brief   当前 PCB W25Q256 外部 NOR Flash 的 Platform Interface。
  ******************************************************************************
  */

#ifndef PLATFORM_FLASH_H
#define PLATFORM_FLASH_H

#include <stdint.h>

#include "Platform/platform.h"

/** @brief 当前 PCB W25Q256 返回的 JEDEC 三字节芯片标识。 */
typedef struct
{
    uint8_t ManufacturerID;
    uint8_t MemoryType;
    uint8_t CapacityID;
} Platform_Flash_JedecIDTypeDef;

Platform_StatusTypeDef Platform_Flash_Init(void);
Platform_StatusTypeDef Platform_Flash_GetJedecID(
    Platform_Flash_JedecIDTypeDef *jedec_id);

#endif /* PLATFORM_FLASH_H */
