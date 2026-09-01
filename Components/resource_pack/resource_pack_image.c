/**
 ******************************************************************************
 * @file    resource_pack_image.c
 * @brief   RPKC1 IMAGE Metadata V1 解码与语义校验。
 ******************************************************************************
 */

#include "Components/resource_pack/resource_pack.h"
#include "Components/resource_pack/resource_pack_internal.h"

#include <stddef.h>

#define RESOURCE_PACK_IMAGE_METADATA_VERSION  1U
#define RESOURCE_PACK_IMAGE_METADATA_SIZE     40U

/**
 * @brief 解码 IMAGE Metadata V1，并校验当前支持的 LVGL 原始像素布局。
 * @param entry 对应 IMAGE Entry。
 * @param view 对应包内 View。
 * @param metadata 接收已解码 Metadata。
 * @return 类型解码状态。
 */
ResourcePack_StatusTypeDef ResourcePack_DecodeImageMetadata(
    const ResourcePack_EntryTypeDef *entry,
    const ResourcePack_EntryViewTypeDef *view,
    ResourcePack_ImageMetadataTypeDef *metadata)
{
    ResourcePack_StatusTypeDef status;
    uint64_t expected_length;

    if ((entry == NULL) || (view == NULL) || (metadata == NULL))
    {
        return RESOURCE_PACK_INVALID_PARAM;
    }

    if (entry->ResourceType != RESOURCE_PACK_TYPE_IMAGE)
    {
        return RESOURCE_PACK_UNSUPPORTED_TYPE;
    }

    status = ResourcePack_VerifyMetadata(entry, view);
    if (status != RESOURCE_PACK_OK)
    {
        return status;
    }

    if (view->MetadataLength != RESOURCE_PACK_IMAGE_METADATA_SIZE)
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    metadata->MetadataVersion = ResourcePack_InternalReadU32(view->Metadata + 0x00U);
    metadata->MetadataSize = ResourcePack_InternalReadU32(view->Metadata + 0x04U);
    metadata->ImageFormat = ResourcePack_InternalReadU32(view->Metadata + 0x08U);
    metadata->PixelFormat = ResourcePack_InternalReadU32(view->Metadata + 0x0CU);
    metadata->Width = ResourcePack_InternalReadU32(view->Metadata + 0x10U);
    metadata->Height = ResourcePack_InternalReadU32(view->Metadata + 0x14U);
    metadata->StrideBytes = ResourcePack_InternalReadU32(view->Metadata + 0x18U);
    metadata->FrameCount = ResourcePack_InternalReadU32(view->Metadata + 0x1CU);
    metadata->ColorSpace = ResourcePack_InternalReadU32(view->Metadata + 0x20U);
    metadata->AlphaMode = ResourcePack_InternalReadU32(view->Metadata + 0x24U);

    if (metadata->MetadataVersion != RESOURCE_PACK_IMAGE_METADATA_VERSION)
    {
        return RESOURCE_PACK_UNSUPPORTED_VERSION;
    }

    if ((metadata->Width == 0U) ||
        (metadata->Height == 0U) ||
        (metadata->FrameCount == 0U))
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    if (metadata->ImageFormat != RESOURCE_PACK_IMAGE_FORMAT_LVGL_NATIVE)
    {
        return RESOURCE_PACK_UNSUPPORTED_FORMAT;
    }

    if ((metadata->PixelFormat == RESOURCE_PACK_PIXEL_FORMAT_UNKNOWN) ||
        (metadata->StrideBytes == 0U))
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    expected_length = (uint64_t)metadata->StrideBytes *
                      metadata->Height *
                      metadata->FrameCount;

    if (expected_length != entry->DataLength)
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    return RESOURCE_PACK_OK;
}
