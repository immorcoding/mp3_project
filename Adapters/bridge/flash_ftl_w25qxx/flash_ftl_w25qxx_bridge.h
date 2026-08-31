/**
 * @file flash_ftl_w25qxx_bridge.h
 * @brief FTL RawOps 与 W25Qxx 的跨组件接缝，不依赖 HAL 或 RTOS。
 */

#ifndef FLASH_FTL_W25QXX_BRIDGE_H
#define FLASH_FTL_W25QXX_BRIDGE_H

#include "Components/flash_ftl/flash_ftl.h"
#include "Components/w25qxx/w25qxx.h"

/** @brief Platform 长期持有的分区转换上下文；不拥有 Device 或 FTL。 */
typedef struct
{
    W25Qxx_HandleTypeDef *Device;
    uint32_t Base; /* 芯片内分区起始字节地址，4 KiB 对齐。 */
    uint32_t Size; /* 分区字节数，所有 RawOps 相对范围受此限制。 */
} FlashFTL_W25QxxBridgeTypeDef;

FlashFTL_StatusTypeDef FlashFTL_W25QxxBridge_Bind(FlashFTL_HandleTypeDef *ftl,
                                                  FlashFTL_W25QxxBridgeTypeDef *bridge,
                                                  W25Qxx_HandleTypeDef *device,
                                                  uint32_t base,
                                                  uint32_t size,
                                                  const FlashFTL_MemoryTypeDef *memory);

#endif /* FLASH_FTL_W25QXX_BRIDGE_H */
