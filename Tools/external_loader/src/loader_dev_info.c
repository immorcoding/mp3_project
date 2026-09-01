/**
 * @file loader_dev_info.c
 * @brief 导出 STM32CubeProgrammer 识别 W25Q256 所需的存储描述。
 */

#include "loader_api.h"
#include "loader_geometry.h"
#include "loader_w25q256_config.h"

const Loader_StorageInfoTypeDef StorageInfo __attribute__((section(".info"), used)) = {
    "W25Q256JV_STM32H743ZG",
    LOADER_DEVICE_TYPE_NOR_FLASH,
    LOADER_FLASH_BASE_ADDRESS,
    LOADER_FLASH_SIZE_BYTES,
    LOADER_W25Q256_PAGE_SIZE,
    0xFFU,
    {
        {LOADER_FLASH_SIZE_BYTES / LOADER_W25Q256_SECTOR_SIZE,
         LOADER_W25Q256_SECTOR_SIZE},
        {0U, 0U},
    },
};
