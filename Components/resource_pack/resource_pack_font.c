/**
 ******************************************************************************
 * @file    resource_pack_font.c
 * @brief   RPKC1 FONT Metadata V1 解码，以及对 lv_font_conv bin 的位图总量核对。
 ******************************************************************************
 */

#include "Components/resource_pack/resource_pack.h"
#include "Components/resource_pack/resource_pack_internal.h"

#include <stddef.h>
#include <string.h>

#define RESOURCE_PACK_FONT_METADATA_VERSION  1U
#define RESOURCE_PACK_FONT_METADATA_SIZE     32U

/* lv_font_conv bin 的分段：4 B 长度（含本前缀）、4 B 标签、正文。 */
#define RESOURCE_PACK_FONT_SECTION_PREFIX    8U
#define RESOURCE_PACK_FONT_HEAD_SIZE         48U
#define RESOURCE_PACK_FONT_LOCA_FIXED_SIZE   12U
#define RESOURCE_PACK_FONT_MAX_HEADER_BITS   128U
#define RESOURCE_PACK_FONT_MAX_BOX_BITS      16U

/** @brief 从 bin 中核对出的字体事实。 */
typedef struct
{
    uint32_t GlyphCount;
    uint32_t PixelHeight;
    int32_t Ascent;
    int32_t Descent;
    uint64_t BitmapBytes;
} ResourcePack_FontBinInfoTypeDef;

/** @brief 一个已通过范围检查的 bin 分段。 */
typedef struct
{
    uint32_t Offset;
    uint32_t Length;
} ResourcePack_FontSectionTypeDef;

/**
 * @brief 定位并校验一个 bin 分段的标签与范围。
 * @param[in] data bin 首地址。
 * @param[in] length bin 总长度。
 * @param[in] offset 分段起点，相对 bin 起点。
 * @param[in] label 4 字节标签。
 * @param[out] section 接收分段起点与长度。
 * @return 分段完整位于 bin 内且标签相符时返回 1，否则返回 0。
 */
static uint8_t resource_pack_font_find_section(
    const uint8_t *data,
    uint32_t length,
    uint32_t offset,
    const char *label,
    ResourcePack_FontSectionTypeDef *section)
{
    uint32_t section_length;

    if (((uint64_t)offset + RESOURCE_PACK_FONT_SECTION_PREFIX) > length)
    {
        return 0U;
    }

    if (memcmp(data + offset + 4U, label, 4U) != 0)
    {
        return 0U;
    }

    section_length = ResourcePack_InternalReadU32(data + offset);
    if ((section_length < RESOURCE_PACK_FONT_SECTION_PREFIX) ||
        (((uint64_t)offset + section_length) > length))
    {
        return 0U;
    }

    section->Offset = offset;
    section->Length = section_length;
    return 1U;
}

/**
 * @brief 按 MSB 优先从位流读取最多 16 位无符号数。
 * @param[in] data 位流首地址，调用者保证覆盖 bit_position + count 位。
 * @param[in] bit_position 起始位序号。
 * @param[in] count 位数，0 返回 0。
 * @return 读到的数值。
 */
static uint32_t resource_pack_font_read_bits(
    const uint8_t *data,
    uint32_t bit_position,
    uint32_t count)
{
    uint32_t value = 0U;

    for (uint32_t index = 0U; index < count; index++)
    {
        uint32_t position = bit_position + index;
        uint32_t bit = ((uint32_t)data[position / 8U] >> (7U - (position % 8U))) & 1U;

        value = (value << 1U) | bit;
    }

    return value;
}

/**
 * @brief 读取 loca 第 index 项（相对 glyf 分段起点的字节偏移）。
 * @param[in] loca loca 分段首地址。
 * @param[in] entry_size 单项宽度，2 或 4。
 * @param[in] index 项序号，调用者保证在范围内。
 * @return 偏移值。
 */
static uint32_t resource_pack_font_read_loca(
    const uint8_t *loca,
    uint32_t entry_size,
    uint32_t index)
{
    const uint8_t *entry = loca + RESOURCE_PACK_FONT_LOCA_FIXED_SIZE + (index * entry_size);

    return (entry_size == 2U) ? (uint32_t)ResourcePack_InternalReadU16(entry)
                              : ResourcePack_InternalReadU32(entry);
}

/**
 * @brief 解析 bin 的 head、cmap、loca、glyf 分段并统计位图总量。
 *
 * 统计口径与 LVGL 8.3 的 lv_font_load 一致：字形 0（notdef）不计，盒子宽或高为 0 的字形不计，
 * 其余字形的位图字节数为条目长度减去整字节的字形头。cmap 与 kern 的内容由消费者解析，这里只跳过。
 * @param[in] data bin 首地址。
 * @param[in] length bin 总长度。
 * @param[out] info 接收核对结果。
 * @return 结构完整且位图总量小于上限时返回 1，否则返回 0。
 */
static uint8_t resource_pack_font_scan_bin(
    const uint8_t *data,
    uint32_t length,
    ResourcePack_FontBinInfoTypeDef *info)
{
    ResourcePack_FontSectionTypeDef head;
    ResourcePack_FontSectionTypeDef cmap;
    ResourcePack_FontSectionTypeDef loca;
    ResourcePack_FontSectionTypeDef glyf;
    uint32_t index_to_loc_format;
    uint32_t xy_bits;
    uint32_t wh_bits;
    uint32_t advance_bits;
    uint32_t header_bits;
    uint32_t header_bytes;
    uint32_t loca_entry_size;
    uint32_t glyph_count;
    uint32_t box_bit_position;
    uint64_t bitmap_bytes = 0U;

    if (!resource_pack_font_find_section(data, length, 0U, "head", &head) ||
        (head.Length < RESOURCE_PACK_FONT_HEAD_SIZE))
    {
        return 0U;
    }

    info->PixelHeight = ResourcePack_InternalReadU16(data + 0x0EU);
    info->Ascent = (int32_t)ResourcePack_InternalReadU16(data + 0x10U);
    info->Descent = (int32_t)(int16_t)ResourcePack_InternalReadU16(data + 0x12U);
    index_to_loc_format = data[0x22U];
    xy_bits = data[0x26U];
    wh_bits = data[0x27U];
    advance_bits = data[0x28U];

    header_bits = advance_bits + (2U * xy_bits) + (2U * wh_bits);
    if (((index_to_loc_format != 0U) && (index_to_loc_format != 1U)) ||
        (wh_bits > RESOURCE_PACK_FONT_MAX_BOX_BITS) ||
        (header_bits > RESOURCE_PACK_FONT_MAX_HEADER_BITS))
    {
        return 0U;
    }

    header_bytes = (header_bits + 7U) / 8U;
    box_bit_position = advance_bits + (2U * xy_bits);
    loca_entry_size = (index_to_loc_format == 0U) ? 2U : 4U;

    if (!resource_pack_font_find_section(data, length, head.Length, "cmap", &cmap) ||
        !resource_pack_font_find_section(data, length, cmap.Offset + cmap.Length, "loca", &loca) ||
        (loca.Length < RESOURCE_PACK_FONT_LOCA_FIXED_SIZE) ||
        !resource_pack_font_find_section(data, length, loca.Offset + loca.Length, "glyf", &glyf))
    {
        return 0U;
    }

    glyph_count = ResourcePack_InternalReadU32(data + loca.Offset + 8U);
    if ((glyph_count == 0U) ||
        (((uint64_t)RESOURCE_PACK_FONT_LOCA_FIXED_SIZE + ((uint64_t)glyph_count * loca_entry_size)) >
         loca.Length))
    {
        return 0U;
    }

    for (uint32_t index = 0U; index < glyph_count; index++)
    {
        uint32_t offset = resource_pack_font_read_loca(data + loca.Offset, loca_entry_size, index);
        uint32_t next = (index + 1U < glyph_count)
                            ? resource_pack_font_read_loca(data + loca.Offset, loca_entry_size, index + 1U)
                            : glyf.Length;
        const uint8_t *glyph;
        uint32_t box_width;
        uint32_t box_height;

        if ((offset > next) || (next > glyf.Length))
        {
            return 0U;
        }

        /* 字形 0（notdef）被 LVGL 置空，lv_font_conv 为它写入零长度条目，不要求有字形头。 */
        if (index == 0U)
        {
            continue;
        }

        if ((next - offset) < header_bytes)
        {
            return 0U;
        }

        glyph = data + glyf.Offset + offset;
        box_width = resource_pack_font_read_bits(glyph, box_bit_position, wh_bits);
        box_height = resource_pack_font_read_bits(glyph, box_bit_position + wh_bits, wh_bits);

        if ((box_width * box_height) != 0U)
        {
            bitmap_bytes += (uint64_t)(next - offset) - (header_bits / 8U);
            if (bitmap_bytes >= RESOURCE_PACK_FONT_BITMAP_LIMIT_BYTES)
            {
                return 0U;
            }
        }
    }

    info->GlyphCount = glyph_count;
    info->BitmapBytes = bitmap_bytes;
    return 1U;
}

/**
 * @brief 解码 FONT Metadata V1，并核对其与 bin 的一致性及位图总量上限。
 *
 * Data 即 lv_font_conv 未压缩 bin 的原样字节；本函数只做一次顺序扫描，
 * 不建字形表，也不申请内存。字形表与位图重排由消费者负责。
 * @param[in] entry 对应 FONT Entry。
 * @param[in] view 对应包内 View，Data 必须覆盖 entry 声明的完整 bin。
 * @param[out] metadata 接收已解码 Metadata。
 * @return 类型解码状态：Metadata 与 bin 不一致、bin 结构非法或位图总量不小于 1 MiB 时返回格式错误。
 */
ResourcePack_StatusTypeDef ResourcePack_DecodeFontMetadata(
    const ResourcePack_EntryTypeDef *entry,
    const ResourcePack_EntryViewTypeDef *view,
    ResourcePack_FontMetadataTypeDef *metadata)
{
    ResourcePack_StatusTypeDef status;
    ResourcePack_FontBinInfoTypeDef info;

    if ((entry == NULL) || (view == NULL) || (metadata == NULL))
    {
        return RESOURCE_PACK_INVALID_PARAM;
    }

    if (entry->ResourceType != RESOURCE_PACK_TYPE_FONT)
    {
        return RESOURCE_PACK_UNSUPPORTED_TYPE;
    }

    status = ResourcePack_VerifyMetadata(entry, view);
    if (status != RESOURCE_PACK_OK)
    {
        return status;
    }

    if (view->MetadataLength != RESOURCE_PACK_FONT_METADATA_SIZE)
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    metadata->MetadataVersion = ResourcePack_InternalReadU32(view->Metadata + 0x00U);
    metadata->MetadataSize = ResourcePack_InternalReadU32(view->Metadata + 0x04U);
    metadata->FontFormat = ResourcePack_InternalReadU32(view->Metadata + 0x08U);
    metadata->CodepointEncoding = ResourcePack_InternalReadU32(view->Metadata + 0x0CU);
    metadata->GlyphCount = ResourcePack_InternalReadU32(view->Metadata + 0x10U);
    metadata->NominalPixelHeight = ResourcePack_InternalReadU32(view->Metadata + 0x14U);
    metadata->Ascent = (int32_t)ResourcePack_InternalReadU32(view->Metadata + 0x18U);
    metadata->Descent = (int32_t)ResourcePack_InternalReadU32(view->Metadata + 0x1CU);

    if (metadata->MetadataVersion != RESOURCE_PACK_FONT_METADATA_VERSION)
    {
        return RESOURCE_PACK_UNSUPPORTED_VERSION;
    }

    if ((metadata->FontFormat != RESOURCE_PACK_FONT_FORMAT_LV_FONT_CONV_BIN) ||
        (metadata->CodepointEncoding != RESOURCE_PACK_CODEPOINT_ENCODING_UNICODE))
    {
        return RESOURCE_PACK_UNSUPPORTED_FORMAT;
    }

    if ((view->Data == NULL) || (view->DataLength != entry->DataLength) ||
        (metadata->GlyphCount == 0U))
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    if (!resource_pack_font_scan_bin(view->Data, view->DataLength, &info) ||
        (info.GlyphCount != metadata->GlyphCount) ||
        (info.PixelHeight != metadata->NominalPixelHeight) ||
        (info.Ascent != metadata->Ascent) ||
        (info.Descent != metadata->Descent))
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    return RESOURCE_PACK_OK;
}
