/**
 ******************************************************************************
 * @file    resource_pack.c
 * @brief   RPKC1 Header、Entry、范围和 CRC 的通用解析实现。
 ******************************************************************************
 */

#include "Components/resource_pack/resource_pack.h"
#include "Components/resource_pack/resource_pack_config.h"
#include "Components/resource_pack/resource_pack_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define RESOURCE_PACK_FORMAT_VERSION       1U
#define RESOURCE_PACK_HEADER_PREFIX_SIZE   64U
#define RESOURCE_PACK_ENTRY_SIZE           40U
#define RESOURCE_PACK_HEADER_CRC_SIZE       4U
#define RESOURCE_PACK_RESERVED_OFFSET      0x30U
#define RESOURCE_PACK_RESERVED_LENGTH      16U

/** @brief 表示包体中一个非空的半开字节区间。 */
typedef struct
{
    uint32_t Offset;
    uint32_t Length;
} ResourcePack_RangeTypeDef;

/**
 * @brief 从未对齐字节流显式读取小端 uint16。
 * @param data 至少包含两个字节的输入地址。
 * @return 解码后的整数。
 */
uint16_t ResourcePack_InternalReadU16(const uint8_t *data)
{
    return (uint16_t)data[0] |
           ((uint16_t)data[1] << 8U);
}

/**
 * @brief 从未对齐字节流显式读取小端 uint32。
 * @param data 至少包含四个字节的输入地址。
 * @return 解码后的整数。
 */
uint32_t ResourcePack_InternalReadU32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

/**
 * @brief 从未对齐字节流显式读取小端 uint64。
 * @param data 至少包含八个字节的输入地址。
 * @return 解码后的整数。
 */
uint64_t ResourcePack_InternalReadU64(const uint8_t *data)
{
    return (uint64_t)ResourcePack_InternalReadU32(data) |
           ((uint64_t)ResourcePack_InternalReadU32(data + 4U) << 32U);
}

/**
 * @brief 计算 CRC-32/ISO-HDLC 校验值。
 * @param data 输入数据首地址。
 * @param length 输入字节数。
 * @return 计算得到的 CRC32。
 */
static uint32_t resource_pack_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t index = 0U; index < length; index++)
    {
        crc ^= data[index];

        for (uint32_t bit = 0U; bit < 8U; bit++)
        {
            uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }

    return crc ^ 0xFFFFFFFFU;
}

/**
 * @brief 判断数值是否为非零二次幂。
 * @param value 待检查数值。
 * @retval true 是非零二次幂。
 * @retval false 不是二次幂或为零。
 */
static bool resource_pack_is_power_of_two(uint32_t value)
{
    return (value != 0U) && ((value & (value - 1U)) == 0U);
}

/**
 * @brief 检查一个范围是否完整位于包体且不进入 Header。
 * @param offset 相对包首地址的偏移。
 * @param length 非零范围长度。
 * @param header_size Header 长度。
 * @param pack_size 完整包长度。
 * @retval true 范围合法。
 * @retval false 范围为空、进入 Header、溢出或越界。
 */
static bool resource_pack_body_range_is_valid(
    uint32_t offset,
    uint32_t length,
    uint32_t header_size,
    uint32_t pack_size)
{
    uint64_t end = (uint64_t)offset + (uint64_t)length;

    return (length != 0U) &&
           (offset >= header_size) &&
           (end <= pack_size);
}

/**
 * @brief 判断两个非空半开区间是否重叠。
 * @param first 第一个区间。
 * @param second 第二个区间。
 * @retval true 存在至少一个共同字节。
 * @retval false 两区间分离。
 */
static bool resource_pack_ranges_overlap(
    ResourcePack_RangeTypeDef first,
    ResourcePack_RangeTypeDef second)
{
    uint64_t first_end = (uint64_t)first.Offset + first.Length;
    uint64_t second_end = (uint64_t)second.Offset + second.Length;

    return ((uint64_t)first.Offset < second_end) &&
           ((uint64_t)second.Offset < first_end);
}

/**
 * @brief 从已经通过 Header 边界校验的 Entry 表解码一项。
 * @param handle 临时或正式打开的句柄。
 * @param index Entry 索引。
 * @param entry 接收显式字段。
 */
static void resource_pack_decode_entry(
    const ResourcePack_HandleTypeDef *handle,
    uint16_t index,
    ResourcePack_EntryTypeDef *entry)
{
    const uint8_t *source = handle->Pack + handle->Info.EntryOffset +
                            (uint32_t)index * handle->Info.EntrySize;

    entry->ResourceID = ResourcePack_InternalReadU32(source + 0x00U);
    entry->ResourceType = ResourcePack_InternalReadU16(source + 0x04U);
    entry->Flags = ResourcePack_InternalReadU16(source + 0x06U);
    entry->ResourceVersion = ResourcePack_InternalReadU64(source + 0x08U);
    entry->DataOffset = ResourcePack_InternalReadU32(source + 0x10U);
    entry->DataLength = ResourcePack_InternalReadU32(source + 0x14U);
    entry->DataCRC32 = ResourcePack_InternalReadU32(source + 0x18U);
    entry->MetadataOffset = ResourcePack_InternalReadU32(source + 0x1CU);
    entry->MetadataLength = ResourcePack_InternalReadU32(source + 0x20U);
    entry->MetadataCRC32 = ResourcePack_InternalReadU32(source + 0x24U);
}

/**
 * @brief 校验单个 Entry 的公共字段、范围和对齐。
 * @param handle 已解码 Header 的临时句柄。
 * @param entry 待校验 Entry。
 * @return RPKC1 公共格式状态。
 */
static ResourcePack_StatusTypeDef resource_pack_validate_entry(
    const ResourcePack_HandleTypeDef *handle,
    const ResourcePack_EntryTypeDef *entry)
{
    bool metadata_is_empty = (entry->MetadataOffset == 0U) &&
                             (entry->MetadataLength == 0U) &&
                             (entry->MetadataCRC32 == 0U);
    bool metadata_is_partial = (entry->MetadataOffset == 0U) ||
                               (entry->MetadataLength == 0U);

    if ((entry->ResourceID == 0U) ||
        (entry->ResourceType == RESOURCE_PACK_TYPE_INVALID) ||
        (entry->ResourceType == 0xFFFFU) ||
        (entry->Flags != 0U) ||
        !resource_pack_body_range_is_valid(
            entry->DataOffset,
            entry->DataLength,
            handle->Info.HeaderSize,
            handle->Info.PackSize) ||
        ((entry->DataOffset % handle->Info.DataAlignment) != 0U))
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    if (!metadata_is_empty)
    {
        ResourcePack_RangeTypeDef metadata_range;
        ResourcePack_RangeTypeDef data_range;

        if (metadata_is_partial ||
            !resource_pack_body_range_is_valid(
                entry->MetadataOffset,
                entry->MetadataLength,
                handle->Info.HeaderSize,
                handle->Info.PackSize) ||
            ((entry->MetadataOffset % handle->Info.MetadataAlignment) != 0U))
        {
            return RESOURCE_PACK_FORMAT_ERROR;
        }

        metadata_range.Offset = entry->MetadataOffset;
        metadata_range.Length = entry->MetadataLength;
        data_range.Offset = entry->DataOffset;
        data_range.Length = entry->DataLength;

        if (resource_pack_ranges_overlap(metadata_range, data_range))
        {
            return RESOURCE_PACK_FORMAT_ERROR;
        }
    }

    if (((entry->ResourceType >= RESOURCE_PACK_TYPE_FONT) &&
         (entry->ResourceType <= RESOURCE_PACK_TYPE_FIRMWARE)) &&
        metadata_is_empty)
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    return RESOURCE_PACK_OK;
}

/**
 * @brief 校验当前 Entry 的区域未与更早 Entry 重叠。
 * @param handle 已解码 Header 的临时句柄。
 * @param current_index 当前 Entry 索引。
 * @param current 当前 Entry。
 * @return 区域独占状态。
 */
static ResourcePack_StatusTypeDef resource_pack_validate_previous_regions(
    const ResourcePack_HandleTypeDef *handle,
    uint16_t current_index,
    const ResourcePack_EntryTypeDef *current)
{
    ResourcePack_RangeTypeDef current_ranges[2] = {
        {current->DataOffset, current->DataLength},
        {current->MetadataOffset, current->MetadataLength}
    };

    for (uint16_t index = 0U; index < current_index; index++)
    {
        ResourcePack_EntryTypeDef previous;
        ResourcePack_RangeTypeDef previous_ranges[2];

        resource_pack_decode_entry(handle, index, &previous);
        previous_ranges[0].Offset = previous.DataOffset;
        previous_ranges[0].Length = previous.DataLength;
        previous_ranges[1].Offset = previous.MetadataOffset;
        previous_ranges[1].Length = previous.MetadataLength;

        for (uint32_t current_range = 0U; current_range < 2U; current_range++)
        {
            if (current_ranges[current_range].Length == 0U)
            {
                continue;
            }

            for (uint32_t previous_range = 0U; previous_range < 2U; previous_range++)
            {
                if ((previous_ranges[previous_range].Length != 0U) &&
                    resource_pack_ranges_overlap(current_ranges[current_range],
                                                 previous_ranges[previous_range]))
                {
                    return RESOURCE_PACK_FORMAT_ERROR;
                }
            }
        }
    }

    return RESOURCE_PACK_OK;
}

/**
 * @brief 打开并完整校验 RPKC1 Header、Entry 公共规则和所有区域布局。
 * @param pack 可读内存窗口中的包首地址。
 * @param available_size 从 pack 起可安全读取的窗口长度。
 * @param handle 接收零动态内存句柄。
 * @return 打开状态。
 * @note 本函数不扫描大型 Data，也不验证类型专用 Metadata 内容。
 */
ResourcePack_StatusTypeDef ResourcePack_Open(
    const uint8_t *pack,
    uint32_t available_size,
    ResourcePack_HandleTypeDef *handle)
{
    uint64_t entry_table_end;
    uint32_t expected_header_crc;
    uint32_t actual_header_crc;
    uint32_t previous_resource_id = 0U;

    if ((pack == NULL) || (handle == NULL))
    {
        return RESOURCE_PACK_INVALID_PARAM;
    }

    memset(handle, 0, sizeof(*handle));

    if ((available_size < RESOURCE_PACK_HEADER_PREFIX_SIZE) ||
        (memcmp(pack, "RPKC", 4U) != 0))
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    handle->Pack = pack;
    handle->AvailableSize = available_size;
    handle->Info.FormatVersion = ResourcePack_InternalReadU32(pack + 0x04U);
    handle->Info.HeaderSize = ResourcePack_InternalReadU32(pack + 0x08U);
    handle->Info.PackSize = ResourcePack_InternalReadU32(pack + 0x0CU);
    handle->Info.EntryOffset = ResourcePack_InternalReadU32(pack + 0x10U);
    handle->Info.EntryCount = ResourcePack_InternalReadU16(pack + 0x14U);
    handle->Info.EntrySize = ResourcePack_InternalReadU16(pack + 0x16U);
    handle->Info.DataAlignment = ResourcePack_InternalReadU32(pack + 0x18U);
    handle->Info.MetadataAlignment = ResourcePack_InternalReadU32(pack + 0x1CU);
    handle->Info.VendorID = ResourcePack_InternalReadU32(pack + 0x20U);
    handle->Info.ProductID = ResourcePack_InternalReadU32(pack + 0x24U);
    handle->Info.PackageVersion = ResourcePack_InternalReadU64(pack + 0x28U);

    if ((handle->Info.FormatVersion != RESOURCE_PACK_FORMAT_VERSION) ||
        (handle->Info.HeaderSize < RESOURCE_PACK_HEADER_PREFIX_SIZE +
                                       RESOURCE_PACK_HEADER_CRC_SIZE) ||
        (handle->Info.HeaderSize > handle->Info.PackSize) ||
        (handle->Info.PackSize > available_size) ||
        (handle->Info.EntryOffset < RESOURCE_PACK_HEADER_PREFIX_SIZE) ||
        (handle->Info.EntrySize != RESOURCE_PACK_ENTRY_SIZE) ||
        !resource_pack_is_power_of_two(handle->Info.DataAlignment) ||
        (handle->Info.DataAlignment < 4U) ||
        !resource_pack_is_power_of_two(handle->Info.MetadataAlignment))
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    entry_table_end = (uint64_t)handle->Info.EntryOffset +
                      (uint64_t)handle->Info.EntryCount * handle->Info.EntrySize;

    if (entry_table_end > handle->Info.HeaderSize - RESOURCE_PACK_HEADER_CRC_SIZE)
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    for (uint32_t index = RESOURCE_PACK_HEADER_PREFIX_SIZE;
         index < handle->Info.EntryOffset;
         index++)
    {
        if (pack[index] != 0xFFU)
        {
            return RESOURCE_PACK_FORMAT_ERROR;
        }
    }

    for (uint32_t index = (uint32_t)entry_table_end;
         index < handle->Info.HeaderSize - RESOURCE_PACK_HEADER_CRC_SIZE;
         index++)
    {
        if (pack[index] != 0xFFU)
        {
            return RESOURCE_PACK_FORMAT_ERROR;
        }
    }

    for (uint32_t index = 0U; index < RESOURCE_PACK_RESERVED_LENGTH; index++)
    {
        if (pack[RESOURCE_PACK_RESERVED_OFFSET + index] != 0xFFU)
        {
            return RESOURCE_PACK_FORMAT_ERROR;
        }
    }

    expected_header_crc = ResourcePack_InternalReadU32(
        pack + handle->Info.HeaderSize - RESOURCE_PACK_HEADER_CRC_SIZE);
    actual_header_crc = resource_pack_crc32(
        pack,
        handle->Info.HeaderSize - RESOURCE_PACK_HEADER_CRC_SIZE);

    if (actual_header_crc != expected_header_crc)
    {
        return RESOURCE_PACK_CRC_ERROR;
    }

    for (uint16_t index = 0U; index < handle->Info.EntryCount; index++)
    {
        ResourcePack_EntryTypeDef entry;
        ResourcePack_StatusTypeDef status;

        resource_pack_decode_entry(handle, index, &entry);

        if ((index != 0U) && (entry.ResourceID <= previous_resource_id))
        {
            return RESOURCE_PACK_FORMAT_ERROR;
        }

        status = resource_pack_validate_entry(handle, &entry);
        if (status != RESOURCE_PACK_OK)
        {
            return status;
        }

        status = resource_pack_validate_previous_regions(handle, index, &entry);
        if (status != RESOURCE_PACK_OK)
        {
            return status;
        }

        previous_resource_id = entry.ResourceID;
    }

    handle->IsOpen = 1U;
    return RESOURCE_PACK_OK;
}

/**
 * @brief 取得已打开包的公共头信息副本。
 * @param handle 已打开句柄。
 * @param info 接收公共信息。
 * @return 查询状态。
 */
ResourcePack_StatusTypeDef ResourcePack_GetInfo(
    const ResourcePack_HandleTypeDef *handle,
    ResourcePack_InfoTypeDef *info)
{
    if (info == NULL)
    {
        return RESOURCE_PACK_INVALID_PARAM;
    }

    if ((handle == NULL) || (handle->IsOpen == 0U))
    {
        return RESOURCE_PACK_NOT_OPEN;
    }

    *info = handle->Info;
    return RESOURCE_PACK_OK;
}

/**
 * @brief 取得已打开包的 Entry 数量。
 * @param handle 已打开句柄。
 * @param entry_count 接收 Entry 数量。
 * @return 查询状态。
 */
ResourcePack_StatusTypeDef ResourcePack_GetEntryCount(
    const ResourcePack_HandleTypeDef *handle,
    uint16_t *entry_count)
{
    if (entry_count == NULL)
    {
        return RESOURCE_PACK_INVALID_PARAM;
    }

    if ((handle == NULL) || (handle->IsOpen == 0U))
    {
        return RESOURCE_PACK_NOT_OPEN;
    }

    *entry_count = handle->Info.EntryCount;
    return RESOURCE_PACK_OK;
}

/**
 * @brief 按索引显式解码一个 Entry。
 * @param handle 已打开句柄。
 * @param index 从零开始的 Entry 索引。
 * @param entry 接收 Entry 字段。
 * @return 查询状态。
 */
ResourcePack_StatusTypeDef ResourcePack_GetEntry(
    const ResourcePack_HandleTypeDef *handle,
    uint16_t index,
    ResourcePack_EntryTypeDef *entry)
{
    if (entry == NULL)
    {
        return RESOURCE_PACK_INVALID_PARAM;
    }

    if ((handle == NULL) || (handle->IsOpen == 0U))
    {
        return RESOURCE_PACK_NOT_OPEN;
    }

    if (index >= handle->Info.EntryCount)
    {
        return RESOURCE_PACK_NOT_FOUND;
    }

    resource_pack_decode_entry(handle, index, entry);
    return RESOURCE_PACK_OK;
}

/**
 * @brief 通过严格升序 Entry 表二分查找稳定 ResourceID。
 * @param handle 已打开句柄。
 * @param resource_id 非零资源身份。
 * @param entry 接收匹配 Entry。
 * @return 查找状态。
 */
ResourcePack_StatusTypeDef ResourcePack_FindEntry(
    const ResourcePack_HandleTypeDef *handle,
    uint32_t resource_id,
    ResourcePack_EntryTypeDef *entry)
{
    uint32_t low;
    uint32_t high;

    if ((resource_id == 0U) || (entry == NULL))
    {
        return RESOURCE_PACK_INVALID_PARAM;
    }

    if ((handle == NULL) || (handle->IsOpen == 0U))
    {
        return RESOURCE_PACK_NOT_OPEN;
    }

    low = 0U;
    high = handle->Info.EntryCount;

    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2U;
        ResourcePack_EntryTypeDef candidate;

        resource_pack_decode_entry(handle, (uint16_t)middle, &candidate);

        if (candidate.ResourceID == resource_id)
        {
            *entry = candidate;
            return RESOURCE_PACK_OK;
        }

        if (candidate.ResourceID < resource_id)
        {
            low = middle + 1U;
        }
        else
        {
            high = middle;
        }
    }

    return RESOURCE_PACK_NOT_FOUND;
}

/**
 * @brief 建立一个只在映射窗口有效期间使用的包内只读 View。
 * @param handle 已打开句柄。
 * @param entry 由同一句柄取得的 Entry。
 * @param view 接收 Metadata 和 Data 指针。
 * @return 建立 View 的状态。
 */
ResourcePack_StatusTypeDef ResourcePack_GetEntryView(
    const ResourcePack_HandleTypeDef *handle,
    const ResourcePack_EntryTypeDef *entry,
    ResourcePack_EntryViewTypeDef *view)
{
    if ((entry == NULL) || (view == NULL))
    {
        return RESOURCE_PACK_INVALID_PARAM;
    }

    if ((handle == NULL) || (handle->IsOpen == 0U))
    {
        return RESOURCE_PACK_NOT_OPEN;
    }

    if (resource_pack_validate_entry(handle, entry) != RESOURCE_PACK_OK)
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    view->Metadata = (entry->MetadataLength != 0U) ?
                         handle->Pack + entry->MetadataOffset :
                         NULL;
    view->MetadataLength = entry->MetadataLength;
    view->Data = handle->Pack + entry->DataOffset;
    view->DataLength = entry->DataLength;
    return RESOURCE_PACK_OK;
}

/**
 * @brief 校验 Metadata CRC、公共前缀和当前已启用的资源类型。
 * @param entry 对应 Entry。
 * @param view 对应包内 View。
 * @return Metadata 公共校验状态。
 */
ResourcePack_StatusTypeDef ResourcePack_VerifyMetadata(
    const ResourcePack_EntryTypeDef *entry,
    const ResourcePack_EntryViewTypeDef *view)
{
    uint32_t metadata_size;

    if ((entry == NULL) || (view == NULL) ||
        (entry->MetadataLength != view->MetadataLength))
    {
        return RESOURCE_PACK_INVALID_PARAM;
    }

    if (entry->MetadataLength == 0U)
    {
        return (entry->ResourceType == RESOURCE_PACK_TYPE_BINARY) ?
                   RESOURCE_PACK_OK :
                   RESOURCE_PACK_FORMAT_ERROR;
    }

    if ((view->Metadata == NULL) || (view->MetadataLength < 8U))
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    if (resource_pack_crc32(view->Metadata, view->MetadataLength) != entry->MetadataCRC32)
    {
        return RESOURCE_PACK_CRC_ERROR;
    }

    metadata_size = ResourcePack_InternalReadU32(view->Metadata + 4U);
    if (metadata_size != view->MetadataLength)
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    switch (entry->ResourceType)
    {
        case RESOURCE_PACK_TYPE_BINARY:
#if RESOURCE_PACK_TYPE_BINARY_ENABLE
            return RESOURCE_PACK_OK;
#else
            return RESOURCE_PACK_UNSUPPORTED_TYPE;
#endif

        case RESOURCE_PACK_TYPE_IMAGE:
#if RESOURCE_PACK_TYPE_IMAGE_ENABLE
            return RESOURCE_PACK_OK;
#else
            return RESOURCE_PACK_UNSUPPORTED_TYPE;
#endif

        case RESOURCE_PACK_TYPE_FONT:
#if RESOURCE_PACK_TYPE_FONT_ENABLE
            return RESOURCE_PACK_OK;
#else
            return RESOURCE_PACK_UNSUPPORTED_TYPE;
#endif

        case RESOURCE_PACK_TYPE_AUDIO:
#if RESOURCE_PACK_TYPE_AUDIO_ENABLE
            return RESOURCE_PACK_OK;
#else
            return RESOURCE_PACK_UNSUPPORTED_TYPE;
#endif

        case RESOURCE_PACK_TYPE_MODEL:
#if RESOURCE_PACK_TYPE_MODEL_ENABLE
            return RESOURCE_PACK_OK;
#else
            return RESOURCE_PACK_UNSUPPORTED_TYPE;
#endif

        case RESOURCE_PACK_TYPE_FIRMWARE:
#if RESOURCE_PACK_TYPE_FIRMWARE_ENABLE
            return RESOURCE_PACK_OK;
#else
            return RESOURCE_PACK_UNSUPPORTED_TYPE;
#endif

        default:
            return RESOURCE_PACK_UNSUPPORTED_TYPE;
    }
}

/**
 * @brief 校验任意有效缓冲区的 CRC-32/ISO-HDLC。
 * @param buffer 输入数据。
 * @param length 非零输入长度。
 * @param expected_crc32 期望校验值。
 * @return CRC 校验状态。
 */
ResourcePack_StatusTypeDef ResourcePack_VerifyBuffer(
    const void *buffer,
    uint32_t length,
    uint32_t expected_crc32)
{
    if ((buffer == NULL) || (length == 0U))
    {
        return RESOURCE_PACK_INVALID_PARAM;
    }

    return (resource_pack_crc32(buffer, length) == expected_crc32) ?
               RESOURCE_PACK_OK :
               RESOURCE_PACK_CRC_ERROR;
}
