/**
 ******************************************************************************
 * @file    resource_pack_binary.c
 * @brief   RPKC1 BINARY Metadata V1 解码与语义校验。
 ******************************************************************************
 */

#include "Components/resource_pack/resource_pack.h"
#include "Components/resource_pack/resource_pack_internal.h"

#include <stddef.h>

#define RESOURCE_PACK_BINARY_METADATA_VERSION  1U
#define RESOURCE_PACK_BINARY_METADATA_SIZE     24U

/**
 * @brief 解码 BINARY Metadata V1，并核对非 OPAQUE 数组的完整数据长度。
 * @param entry 对应 BINARY Entry。
 * @param view 对应包内 View。
 * @param metadata 接收已解码 Metadata。
 * @return 类型解码状态。
 */
ResourcePack_StatusTypeDef ResourcePack_DecodeBinaryMetadata(
    const ResourcePack_EntryTypeDef *entry,
    const ResourcePack_EntryViewTypeDef *view,
    ResourcePack_BinaryMetadataTypeDef *metadata)
{
    ResourcePack_StatusTypeDef status;

    if ((entry == NULL) || (view == NULL) || (metadata == NULL))
    {
        return RESOURCE_PACK_INVALID_PARAM;
    }

    if (entry->ResourceType != RESOURCE_PACK_TYPE_BINARY)
    {
        return RESOURCE_PACK_UNSUPPORTED_TYPE;
    }

    status = ResourcePack_VerifyMetadata(entry, view);
    if (status != RESOURCE_PACK_OK)
    {
        return status;
    }

    if (view->MetadataLength == 0U)
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    if (view->MetadataLength != RESOURCE_PACK_BINARY_METADATA_SIZE)
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    metadata->MetadataVersion = ResourcePack_InternalReadU32(view->Metadata + 0x00U);
    metadata->MetadataSize = ResourcePack_InternalReadU32(view->Metadata + 0x04U);
    metadata->ElementFormat = ResourcePack_InternalReadU32(view->Metadata + 0x08U);
    metadata->ByteOrder = ResourcePack_InternalReadU32(view->Metadata + 0x0CU);
    metadata->ElementSize = ResourcePack_InternalReadU32(view->Metadata + 0x10U);
    metadata->ElementCount = ResourcePack_InternalReadU32(view->Metadata + 0x14U);

    if (metadata->MetadataVersion != RESOURCE_PACK_BINARY_METADATA_VERSION)
    {
        return RESOURCE_PACK_UNSUPPORTED_VERSION;
    }

    if (metadata->ElementFormat == RESOURCE_PACK_ELEMENT_FORMAT_INVALID)
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    if (metadata->ElementFormat != RESOURCE_PACK_ELEMENT_FORMAT_OPAQUE)
    {
        uint64_t expected_length;

        if ((metadata->ElementSize == 0U) ||
            (metadata->ElementCount == 0U) ||
            (metadata->ByteOrder == RESOURCE_PACK_BYTE_ORDER_UNKNOWN))
        {
            return RESOURCE_PACK_FORMAT_ERROR;
        }

        expected_length = (uint64_t)metadata->ElementSize * metadata->ElementCount;
        if (expected_length != entry->DataLength)
        {
            return RESOURCE_PACK_FORMAT_ERROR;
        }
    }

    return RESOURCE_PACK_OK;
}
