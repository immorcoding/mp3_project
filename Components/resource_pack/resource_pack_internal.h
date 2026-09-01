/**
 ******************************************************************************
 * @file    resource_pack_internal.h
 * @brief   RPKC1 Component 内部共享的显式字段读取 Interface。
 ******************************************************************************
 */

#ifndef RESOURCE_PACK_INTERNAL_H
#define RESOURCE_PACK_INTERNAL_H

#include <stdint.h>

uint16_t ResourcePack_InternalReadU16(const uint8_t *data);
uint32_t ResourcePack_InternalReadU32(const uint8_t *data);
uint64_t ResourcePack_InternalReadU64(const uint8_t *data);

#endif /* RESOURCE_PACK_INTERNAL_H */
