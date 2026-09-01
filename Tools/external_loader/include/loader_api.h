/**
 * @file loader_api.h
 * @brief STM32CubeProgrammer 外部烧录算法 ABI 所需类型。
 */

#ifndef LOADER_API_H
#define LOADER_API_H

#include <stdint.h>

#define LOADER_DEVICE_TYPE_NOR_FLASH 3U
#define LOADER_MAX_SECTOR_TYPES       10U

typedef struct
{
    uint32_t SectorNum;
    uint32_t SectorSize;
} Loader_DeviceSectorTypeDef;

typedef struct
{
    char DeviceName[100];
    uint16_t DeviceType;
    uint32_t DeviceStartAddress;
    uint32_t DeviceSize;
    uint32_t PageSize;
    uint8_t EraseValue;
    Loader_DeviceSectorTypeDef Sectors[LOADER_MAX_SECTOR_TYPES];
} Loader_StorageInfoTypeDef;

#endif /* LOADER_API_H */
