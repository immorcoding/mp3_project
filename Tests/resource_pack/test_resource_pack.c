/**
 ******************************************************************************
 * @file    test_resource_pack.c
 * @brief   RPKC1 Component 的主机协议行为测试。
 ******************************************************************************
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Components/resource_pack/resource_pack.h"

#define TEST_HEADER_SIZE  0x1000U
#define TEST_PACKAGE_SIZE 0x3000U
#define TEST_METADATA_OFFSET 0x1000U
#define TEST_DATA_OFFSET  0x2000U

static uint8_t test_package[TEST_PACKAGE_SIZE];

/**
 * @brief 将 16 位整数按小端写入测试包。
 * @param destination 字段首地址。
 * @param value 待写数值。
 */
static void test_write_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
}

/**
 * @brief 将 32 位整数按小端写入测试包。
 * @param destination 字段首地址。
 * @param value 待写数值。
 */
static void test_write_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

/**
 * @brief 将 64 位整数按小端写入测试包。
 * @param destination 字段首地址。
 * @param value 待写数值。
 */
static void test_write_u64(uint8_t *destination, uint64_t value)
{
    test_write_u32(destination, (uint32_t)value);
    test_write_u32(destination + 4U, (uint32_t)(value >> 32U));
}

/**
 * @brief 使用与 RPKC1 相同的 CRC-32/ISO-HDLC 计算测试期望值。
 * @param data 输入数据。
 * @param length 输入长度。
 * @return CRC32 校验值。
 */
static uint32_t test_crc32(const uint8_t *data, uint32_t length)
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
 * @brief 构造一个含 BINARY Metadata 的最小合法 RPKC1 测试包。
 */
static void test_build_valid_package(void)
{
    uint8_t *entry = test_package + 0x40U;
    uint8_t *metadata = test_package + TEST_METADATA_OFFSET;
    uint8_t *data = test_package + TEST_DATA_OFFSET;

    memset(test_package, 0xFF, sizeof(test_package));

    memcpy(test_package, "RPKC", 4U);
    test_write_u32(test_package + 0x04U, 1U);
    test_write_u32(test_package + 0x08U, TEST_HEADER_SIZE);
    test_write_u32(test_package + 0x0CU, TEST_PACKAGE_SIZE);
    test_write_u32(test_package + 0x10U, 0x40U);
    test_write_u16(test_package + 0x14U, 1U);
    test_write_u16(test_package + 0x16U, 40U);
    test_write_u32(test_package + 0x18U, 4096U);
    test_write_u32(test_package + 0x1CU, 4U);
    test_write_u32(test_package + 0x20U, 1U);
    test_write_u32(test_package + 0x24U, 1U);
    test_write_u64(test_package + 0x28U, 9U);

    test_write_u32(metadata + 0x00U, 1U);
    test_write_u32(metadata + 0x04U, 24U);
    test_write_u32(metadata + 0x08U, RESOURCE_PACK_ELEMENT_FORMAT_UINT16);
    test_write_u32(metadata + 0x0CU, RESOURCE_PACK_BYTE_ORDER_LITTLE_ENDIAN);
    test_write_u32(metadata + 0x10U, 2U);
    test_write_u32(metadata + 0x14U, 2U);

    data[0] = 0x34U;
    data[1] = 0x12U;
    data[2] = 0x78U;
    data[3] = 0x56U;

    test_write_u32(entry + 0x00U, 7U);
    test_write_u16(entry + 0x04U, RESOURCE_PACK_TYPE_BINARY);
    test_write_u16(entry + 0x06U, 0U);
    test_write_u64(entry + 0x08U, 3U);
    test_write_u32(entry + 0x10U, TEST_DATA_OFFSET);
    test_write_u32(entry + 0x14U, 4U);
    test_write_u32(entry + 0x18U, test_crc32(data, 4U));
    test_write_u32(entry + 0x1CU, TEST_METADATA_OFFSET);
    test_write_u32(entry + 0x20U, 24U);
    test_write_u32(entry + 0x24U, test_crc32(metadata, 24U));

    test_write_u32(
        test_package + TEST_HEADER_SIZE - 4U,
        test_crc32(test_package, TEST_HEADER_SIZE - 4U));
}

/**
 * @brief 在合法基础包上构造一项 2×1 LVGL TRUE_COLOR_ALPHA 图片资源。
 */
static void test_build_valid_image_package(void)
{
    uint8_t *entry;
    uint8_t *metadata;
    uint8_t *data;

    test_build_valid_package();
    entry = test_package + 0x40U;
    metadata = test_package + TEST_METADATA_OFFSET;
    data = test_package + TEST_DATA_OFFSET;

    memset(metadata, 0, 40U);
    test_write_u32(metadata + 0x00U, 1U);
    test_write_u32(metadata + 0x04U, 40U);
    test_write_u32(metadata + 0x08U, RESOURCE_PACK_IMAGE_FORMAT_LVGL_NATIVE);
    test_write_u32(metadata + 0x0CU, RESOURCE_PACK_PIXEL_FORMAT_TRUE_COLOR_ALPHA);
    test_write_u32(metadata + 0x10U, 2U);
    test_write_u32(metadata + 0x14U, 1U);
    test_write_u32(metadata + 0x18U, 6U);
    test_write_u32(metadata + 0x1CU, 1U);
    test_write_u32(metadata + 0x20U, RESOURCE_PACK_COLOR_SPACE_SRGB);
    test_write_u32(metadata + 0x24U, RESOURCE_PACK_ALPHA_MODE_STRAIGHT);

    for (uint32_t index = 0U; index < 6U; index++)
    {
        data[index] = (uint8_t)(index + 1U);
    }

    test_write_u16(entry + 0x04U, RESOURCE_PACK_TYPE_IMAGE);
    test_write_u32(entry + 0x14U, 6U);
    test_write_u32(entry + 0x18U, test_crc32(data, 6U));
    test_write_u32(entry + 0x20U, 40U);
    test_write_u32(entry + 0x24U, test_crc32(metadata, 40U));
    test_write_u32(test_package + TEST_HEADER_SIZE - 4U,
                   test_crc32(test_package, TEST_HEADER_SIZE - 4U));
}

/**
 * @brief 验证合法包可打开、查找、建立 View 并解码 BINARY Metadata。
 */
static void test_open_find_and_decode_binary(void)
{
    ResourcePack_HandleTypeDef handle = {0};
    ResourcePack_InfoTypeDef info;
    ResourcePack_EntryTypeDef entry;
    ResourcePack_EntryViewTypeDef view;
    ResourcePack_BinaryMetadataTypeDef metadata;

    test_build_valid_package();

    assert(ResourcePack_Open(test_package, sizeof(test_package), &handle) == RESOURCE_PACK_OK);
    assert(ResourcePack_GetInfo(&handle, &info) == RESOURCE_PACK_OK);
    assert(info.VendorID == 1U);
    assert(info.ProductID == 1U);
    assert(info.PackageVersion == 9U);
    assert(info.EntryCount == 1U);

    assert(ResourcePack_FindEntry(&handle, 7U, &entry) == RESOURCE_PACK_OK);
    assert(entry.ResourceVersion == 3U);
    assert(ResourcePack_GetEntryView(&handle, &entry, &view) == RESOURCE_PACK_OK);
    assert(ResourcePack_VerifyMetadata(&entry, &view) == RESOURCE_PACK_OK);
    assert(ResourcePack_DecodeBinaryMetadata(&entry, &view, &metadata) == RESOURCE_PACK_OK);
    assert(metadata.ElementFormat == RESOURCE_PACK_ELEMENT_FORMAT_UINT16);
    assert(metadata.ByteOrder == RESOURCE_PACK_BYTE_ORDER_LITTLE_ENDIAN);
    assert(metadata.ElementSize == 2U);
    assert(metadata.ElementCount == 2U);
    assert(ResourcePack_VerifyBuffer(view.Data, view.DataLength, entry.DataCRC32) == RESOURCE_PACK_OK);
}

/**
 * @brief 验证 IMAGE Metadata V1 可解码并核对原始像素长度。
 */
static void test_open_and_decode_image(void)
{
    ResourcePack_HandleTypeDef handle = {0};
    ResourcePack_EntryTypeDef entry;
    ResourcePack_EntryViewTypeDef view;
    ResourcePack_ImageMetadataTypeDef metadata;

    test_build_valid_image_package();

    assert(ResourcePack_Open(test_package, sizeof(test_package), &handle) == RESOURCE_PACK_OK);
    assert(ResourcePack_GetEntry(&handle, 0U, &entry) == RESOURCE_PACK_OK);
    assert(ResourcePack_GetEntryView(&handle, &entry, &view) == RESOURCE_PACK_OK);
    assert(ResourcePack_DecodeImageMetadata(&entry, &view, &metadata) == RESOURCE_PACK_OK);
    assert(metadata.Width == 2U);
    assert(metadata.Height == 1U);
    assert(metadata.StrideBytes == 6U);
    assert(metadata.ColorSpace == RESOURCE_PACK_COLOR_SPACE_SRGB);
    assert(metadata.AlphaMode == RESOURCE_PACK_ALPHA_MODE_STRAIGHT);
}

/**
 * @brief 验证合法未知类型不阻止 Core 打开，但类型 Metadata 解码保持不支持。
 */
static void test_allows_unknown_type_at_core_level(void)
{
    ResourcePack_HandleTypeDef handle = {0};
    ResourcePack_EntryTypeDef entry;
    ResourcePack_EntryViewTypeDef view;

    test_build_valid_package();
    test_write_u16(test_package + 0x40U + 0x04U, 0x8000U);
    test_write_u32(test_package + TEST_HEADER_SIZE - 4U,
                   test_crc32(test_package, TEST_HEADER_SIZE - 4U));

    assert(ResourcePack_Open(test_package, sizeof(test_package), &handle) == RESOURCE_PACK_OK);
    assert(ResourcePack_GetEntry(&handle, 0U, &entry) == RESOURCE_PACK_OK);
    assert(ResourcePack_GetEntryView(&handle, &entry, &view) == RESOURCE_PACK_OK);
    assert(ResourcePack_VerifyMetadata(&entry, &view) == RESOURCE_PACK_UNSUPPORTED_TYPE);
}

/**
 * @brief 验证 Header CRC 损坏时 Open 立即拒绝。
 */
static void test_rejects_corrupt_header(void)
{
    ResourcePack_HandleTypeDef handle = {0};

    test_build_valid_package();
    test_package[0x28U] ^= 1U;

    assert(ResourcePack_Open(test_package, sizeof(test_package), &handle) ==
           RESOURCE_PACK_CRC_ERROR);
}

/**
 * @brief 验证 Metadata 与 Data 重叠时 Open 拒绝该包。
 */
static void test_rejects_overlapping_regions(void)
{
    ResourcePack_HandleTypeDef handle = {0};

    test_build_valid_package();
    test_write_u32(test_package + 0x40U + 0x1CU, TEST_DATA_OFFSET);
    test_write_u32(test_package + TEST_HEADER_SIZE - 4U,
                   test_crc32(test_package, TEST_HEADER_SIZE - 4U));

    assert(ResourcePack_Open(test_package, sizeof(test_package), &handle) ==
           RESOURCE_PACK_FORMAT_ERROR);
}

#define TEST_FONT_METADATA_SIZE   32U
#define TEST_FONT_HEAD_SIZE       48U
#define TEST_FONT_CMAP_SIZE       12U
#define TEST_FONT_ADV_BITS        9U
#define TEST_FONT_XY_BITS         5U
#define TEST_FONT_WH_BITS         4U
#define TEST_FONT_HEADER_BYTES    3U
#define TEST_FONT_MIB             0x100000U
#define TEST_FONT_RESOURCE_ID  5U

static uint8_t test_font_bin[TEST_FONT_MIB + 0x2000U];

/**
 * @brief 把低 count 位的 value 按 MSB 优先写入位流。
 * @param[in,out] destination 位流首地址（需已清零）。
 * @param[in] bit_position 起始位序号。
 * @param[in] value 待写数值。
 * @param[in] count 位数。
 */
static void test_put_bits(uint8_t *destination, uint32_t bit_position, uint32_t value, uint32_t count)
{
    for (uint32_t index = 0U; index < count; index++)
    {
        uint32_t bit = (value >> (count - 1U - index)) & 1U;

        if (bit != 0U)
        {
            uint32_t position = bit_position + index;
            destination[position / 8U] |= (uint8_t)(0x80U >> (position % 8U));
        }
    }
}

/**
 * @brief 返回合成字形在 glyf 中的字节数：位图紧跟 3 字节头，空盒子字形只有 4 字节头位流。
 * @param[in] index 字形索引，0 为 notdef。
 * @param[in] bitmap 字形位图字节数，0 表示空盒子。
 * @return 字形条目字节数。
 */
static uint32_t test_font_glyph_length(uint32_t index, uint32_t bitmap)
{
    if (index == 0U)
    {
        return 0U; /* lv_font_conv 为 notdef 写入零长度条目。 */
    }

    return (bitmap == 0U) ? 4U : (TEST_FONT_HEADER_BYTES + bitmap);
}

/**
 * @brief 构造合成的 lv_font_conv bin：字形 0 为 notdef，其后每个字形占一个条目。
 * @param[in] bitmap_bytes 字形 1.. 的位图字节数；0 表示该字形盒子为空（如空格）。
 * @param[in] glyph_count 字形条目总数（含索引 0）。
 * @param[in] index_to_loc_format loca 条目宽度，0 为 16 位、1 为 32 位。
 * @return bin 总长度。
 */
static uint32_t test_build_font_bin(
    const uint32_t *bitmap_bytes,
    uint32_t glyph_count,
    uint8_t index_to_loc_format)
{
    uint32_t loca_entry_size = (index_to_loc_format == 0U) ? 2U : 4U;
    uint32_t loca_size = 12U + glyph_count * loca_entry_size;
    uint32_t glyf_size = 8U;
    uint32_t offset;
    uint8_t *head = test_font_bin;
    uint8_t *cmap = head + TEST_FONT_HEAD_SIZE;
    uint8_t *loca = cmap + TEST_FONT_CMAP_SIZE;
    uint8_t *glyf;

    memset(test_font_bin, 0, sizeof(test_font_bin));

    test_write_u32(head, TEST_FONT_HEAD_SIZE);
    memcpy(head + 4U, "head", 4U);
    test_write_u32(head + 0x08U, 1U);
    test_write_u16(head + 0x0CU, 4U);
    test_write_u16(head + 0x0EU, 12U);                      /* font_size */
    test_write_u16(head + 0x10U, 13U);                      /* ascent */
    test_write_u16(head + 0x12U, (uint16_t)(int16_t)-3);    /* descent */
    head[0x22U] = index_to_loc_format;
    head[0x23U] = 1U;                                       /* glyph_id_format */
    head[0x24U] = 1U;                                       /* advance_width_format */
    head[0x25U] = 4U;                                       /* bits_per_pixel */
    head[0x26U] = TEST_FONT_XY_BITS;
    head[0x27U] = TEST_FONT_WH_BITS;
    head[0x28U] = TEST_FONT_ADV_BITS;

    test_write_u32(cmap, TEST_FONT_CMAP_SIZE);
    memcpy(cmap + 4U, "cmap", 4U);

    test_write_u32(loca, loca_size);
    memcpy(loca + 4U, "loca", 4U);
    test_write_u32(loca + 8U, glyph_count);

    for (uint32_t index = 0U; index < glyph_count; index++)
    {
        glyf_size += test_font_glyph_length(index, (index == 0U) ? 0U : bitmap_bytes[index - 1U]);
    }

    glyf = loca + loca_size;
    test_write_u32(glyf, glyf_size);
    memcpy(glyf + 4U, "glyf", 4U);

    offset = 8U;
    for (uint32_t index = 0U; index < glyph_count; index++)
    {
        uint32_t bitmap = (index == 0U) ? 0U : bitmap_bytes[index - 1U];

        if (index_to_loc_format == 0U)
        {
            test_write_u16(loca + 12U + index * 2U, (uint16_t)offset);
        }
        else
        {
            test_write_u32(loca + 12U + index * 4U, offset);
        }

        if (index != 0U)
        {
            uint32_t box = (bitmap != 0U) ? 1U : 0U;

            /* 字形头位流：adv(9) ofs_x(5) ofs_y(5) box_w(4) box_h(4)，共 27 位。 */
            test_put_bits(glyf + offset, 0U, 5U, TEST_FONT_ADV_BITS);
            test_put_bits(glyf + offset, 19U, box, TEST_FONT_WH_BITS);
            test_put_bits(glyf + offset, 23U, box, TEST_FONT_WH_BITS);
        }

        offset += test_font_glyph_length(index, bitmap);
    }

    return TEST_FONT_HEAD_SIZE + TEST_FONT_CMAP_SIZE + loca_size + glyf_size;
}

/**
 * @brief 写入 FONT Metadata V1，缺省字段与合成 bin 一致。
 * @param[out] metadata 32 B 目标缓冲。
 * @param[in] glyph_count GlyphCount。
 */
static void test_write_font_metadata(uint8_t *metadata, uint32_t glyph_count)
{
    memset(metadata, 0, TEST_FONT_METADATA_SIZE);
    test_write_u32(metadata + 0x00U, 1U);
    test_write_u32(metadata + 0x04U, TEST_FONT_METADATA_SIZE);
    test_write_u32(metadata + 0x08U, RESOURCE_PACK_FONT_FORMAT_LV_FONT_CONV_BIN);
    test_write_u32(metadata + 0x0CU, RESOURCE_PACK_CODEPOINT_ENCODING_UNICODE);
    test_write_u32(metadata + 0x10U, glyph_count);
    test_write_u32(metadata + 0x14U, 12U);
    test_write_u32(metadata + 0x18U, 13U);
    test_write_u32(metadata + 0x1CU, (uint32_t)(int32_t)-3);
}

/**
 * @brief 以独立缓冲构造 FONT Entry 与 View，并按 Metadata 内容计算 CRC。
 * @param[in] metadata 32 B Metadata。
 * @param[in] data_length bin 长度。
 * @param[out] entry 接收 Entry。
 * @param[out] view 接收 View，Data 指向 test_font_bin。
 */
static void test_make_font_entry(
    const uint8_t *metadata,
    uint32_t data_length,
    ResourcePack_EntryTypeDef *entry,
    ResourcePack_EntryViewTypeDef *view)
{
    memset(entry, 0, sizeof(*entry));
    entry->ResourceID = TEST_FONT_RESOURCE_ID;
    entry->ResourceType = RESOURCE_PACK_TYPE_FONT;
    entry->ResourceVersion = 1U;
    entry->DataLength = data_length;
    entry->MetadataLength = TEST_FONT_METADATA_SIZE;
    entry->MetadataCRC32 = test_crc32(metadata, TEST_FONT_METADATA_SIZE);

    view->Metadata = metadata;
    view->MetadataLength = TEST_FONT_METADATA_SIZE;
    view->Data = test_font_bin;
    view->DataLength = data_length;
}

/**
 * @brief 验证包内 FONT Entry 可经 Open/Find/View 取得，并解码出 Metadata 各字段。
 */
static void test_open_and_decode_font(void)
{
    static const uint32_t bitmaps[] = {9U, 0U, 7U};
    ResourcePack_HandleTypeDef handle = {0};
    ResourcePack_EntryTypeDef entry;
    ResourcePack_EntryViewTypeDef view;
    ResourcePack_FontMetadataTypeDef metadata;
    uint8_t *package_entry = test_package + 0x40U;
    uint32_t bin_length = test_build_font_bin(bitmaps, 4U, 1U);

    test_build_valid_package();
    test_write_font_metadata(test_package + TEST_METADATA_OFFSET, 4U);
    memcpy(test_package + TEST_DATA_OFFSET, test_font_bin, bin_length);
    test_write_u16(package_entry + 0x04U, RESOURCE_PACK_TYPE_FONT);
    test_write_u32(package_entry + 0x14U, bin_length);
    test_write_u32(package_entry + 0x18U, test_crc32(test_font_bin, bin_length));
    test_write_u32(package_entry + 0x20U, TEST_FONT_METADATA_SIZE);
    test_write_u32(package_entry + 0x24U,
                   test_crc32(test_package + TEST_METADATA_OFFSET, TEST_FONT_METADATA_SIZE));
    test_write_u32(test_package + TEST_HEADER_SIZE - 4U,
                   test_crc32(test_package, TEST_HEADER_SIZE - 4U));

    assert(ResourcePack_Open(test_package, sizeof(test_package), &handle) == RESOURCE_PACK_OK);
    assert(ResourcePack_FindEntry(&handle, 7U, &entry) == RESOURCE_PACK_OK);
    assert(ResourcePack_GetEntryView(&handle, &entry, &view) == RESOURCE_PACK_OK);
    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) == RESOURCE_PACK_OK);
    assert(metadata.MetadataVersion == 1U);
    assert(metadata.MetadataSize == 32U);
    assert(metadata.FontFormat == RESOURCE_PACK_FONT_FORMAT_LV_FONT_CONV_BIN);
    assert(metadata.CodepointEncoding == RESOURCE_PACK_CODEPOINT_ENCODING_UNICODE);
    assert(metadata.GlyphCount == 4U);
    assert(metadata.NominalPixelHeight == 12U);
    assert(metadata.Ascent == 13);
    assert(metadata.Descent == -3);
    assert(ResourcePack_VerifyBuffer(view.Data, view.DataLength, entry.DataCRC32) == RESOURCE_PACK_OK);
}

/**
 * @brief 验证 16 位与 32 位 loca 格式的合法字体都能解码。
 */
static void test_decodes_both_loca_formats(void)
{
    static const uint32_t bitmaps[] = {9U, 0U, 7U};
    uint8_t metadata_bytes[TEST_FONT_METADATA_SIZE];
    ResourcePack_EntryTypeDef entry;
    ResourcePack_EntryViewTypeDef view;
    ResourcePack_FontMetadataTypeDef metadata;

    for (uint8_t format = 0U; format <= 1U; format++)
    {
        uint32_t bin_length = test_build_font_bin(bitmaps, 4U, format);

        test_write_font_metadata(metadata_bytes, 4U);
        test_make_font_entry(metadata_bytes, bin_length, &entry, &view);
        assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) == RESOURCE_PACK_OK);
    }
}

/**
 * @brief 验证 MetadataSize 不是 32 B 时按格式错误拒绝。
 */
static void test_font_rejects_wrong_metadata_size(void)
{
    static const uint32_t bitmaps[] = {9U};
    uint8_t metadata_bytes[TEST_FONT_METADATA_SIZE];
    ResourcePack_EntryTypeDef entry;
    ResourcePack_EntryViewTypeDef view;
    ResourcePack_FontMetadataTypeDef metadata;
    uint32_t bin_length = test_build_font_bin(bitmaps, 2U, 1U);

    test_write_font_metadata(metadata_bytes, 2U);
    test_write_u32(metadata_bytes + 0x04U, 24U);
    test_make_font_entry(metadata_bytes, bin_length, &entry, &view);
    entry.MetadataLength = 24U;
    entry.MetadataCRC32 = test_crc32(metadata_bytes, 24U);
    view.MetadataLength = 24U;

    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) == RESOURCE_PACK_FORMAT_ERROR);
}

/**
 * @brief 验证未知 FontFormat、未知 CodepointEncoding 与未知版本分别返回不支持。
 */
static void test_font_rejects_unknown_enumerations(void)
{
    static const uint32_t bitmaps[] = {9U};
    uint8_t metadata_bytes[TEST_FONT_METADATA_SIZE];
    ResourcePack_EntryTypeDef entry;
    ResourcePack_EntryViewTypeDef view;
    ResourcePack_FontMetadataTypeDef metadata;
    uint32_t bin_length = test_build_font_bin(bitmaps, 2U, 1U);

    test_write_font_metadata(metadata_bytes, 2U);
    test_write_u32(metadata_bytes + 0x08U, 0x7FU);
    test_make_font_entry(metadata_bytes, bin_length, &entry, &view);
    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) ==
           RESOURCE_PACK_UNSUPPORTED_FORMAT);

    test_write_font_metadata(metadata_bytes, 2U);
    test_write_u32(metadata_bytes + 0x0CU, 0x7FU);
    test_make_font_entry(metadata_bytes, bin_length, &entry, &view);
    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) ==
           RESOURCE_PACK_UNSUPPORTED_FORMAT);

    test_write_font_metadata(metadata_bytes, 2U);
    test_write_u32(metadata_bytes + 0x00U, 2U);
    test_make_font_entry(metadata_bytes, bin_length, &entry, &view);
    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) ==
           RESOURCE_PACK_UNSUPPORTED_VERSION);
}

/**
 * @brief 验证类型不是 FONT 时返回不支持类型。
 */
static void test_font_decoder_rejects_other_types(void)
{
    uint8_t metadata_bytes[TEST_FONT_METADATA_SIZE];
    ResourcePack_EntryTypeDef entry;
    ResourcePack_EntryViewTypeDef view;
    ResourcePack_FontMetadataTypeDef metadata;
    static const uint32_t bitmaps[] = {9U};
    uint32_t bin_length = test_build_font_bin(bitmaps, 2U, 1U);

    test_write_font_metadata(metadata_bytes, 2U);
    test_make_font_entry(metadata_bytes, bin_length, &entry, &view);
    entry.ResourceType = RESOURCE_PACK_TYPE_IMAGE;

    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) ==
           RESOURCE_PACK_UNSUPPORTED_TYPE);
}

/**
 * @brief 验证 Metadata 与 bin 不一致（字形数、字号、行高）或 bin 被截断时按格式错误拒绝。
 */
static void test_font_rejects_metadata_that_disagrees_with_bin(void)
{
    static const uint32_t bitmaps[] = {9U, 0U, 7U};
    uint8_t metadata_bytes[TEST_FONT_METADATA_SIZE];
    ResourcePack_EntryTypeDef entry;
    ResourcePack_EntryViewTypeDef view;
    ResourcePack_FontMetadataTypeDef metadata;
    uint32_t bin_length = test_build_font_bin(bitmaps, 4U, 1U);

    test_write_font_metadata(metadata_bytes, 5U);
    test_make_font_entry(metadata_bytes, bin_length, &entry, &view);
    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) == RESOURCE_PACK_FORMAT_ERROR);

    test_write_font_metadata(metadata_bytes, 4U);
    test_write_u32(metadata_bytes + 0x14U, 14U);
    test_make_font_entry(metadata_bytes, bin_length, &entry, &view);
    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) == RESOURCE_PACK_FORMAT_ERROR);

    test_write_font_metadata(metadata_bytes, 4U);
    test_write_u32(metadata_bytes + 0x18U, 12U);
    test_make_font_entry(metadata_bytes, bin_length, &entry, &view);
    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) == RESOURCE_PACK_FORMAT_ERROR);

    test_write_font_metadata(metadata_bytes, 4U);
    test_make_font_entry(metadata_bytes, bin_length - 5U, &entry, &view);
    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) == RESOURCE_PACK_FORMAT_ERROR);

    test_write_font_metadata(metadata_bytes, 4U);
    test_make_font_entry(metadata_bytes, bin_length, &entry, &view);
    test_font_bin[4U] = 'x';
    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) == RESOURCE_PACK_FORMAT_ERROR);
}

/**
 * @brief 验证位图总量恰为 1 MiB 被拒绝、少 1 B 被接受；空盒子字形不计入总量。
 */
static void test_font_bitmap_total_limit(void)
{
    static const uint32_t over[] = {TEST_FONT_MIB / 2U, TEST_FONT_MIB / 2U, 3U};
    static const uint32_t exact[] = {TEST_FONT_MIB / 2U, TEST_FONT_MIB / 2U};
    static const uint32_t under[] = {TEST_FONT_MIB / 2U, TEST_FONT_MIB / 2U - 1U};
    static const uint32_t blank_glyphs[] = {TEST_FONT_MIB - 1U, 0U, 0U, 0U};
    uint8_t metadata_bytes[TEST_FONT_METADATA_SIZE];
    ResourcePack_EntryTypeDef entry;
    ResourcePack_EntryViewTypeDef view;
    ResourcePack_FontMetadataTypeDef metadata;
    uint32_t bin_length;

    bin_length = test_build_font_bin(over, 4U, 1U);
    test_write_font_metadata(metadata_bytes, 4U);
    test_make_font_entry(metadata_bytes, bin_length, &entry, &view);
    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) == RESOURCE_PACK_FORMAT_ERROR);

    bin_length = test_build_font_bin(exact, 3U, 1U);
    test_write_font_metadata(metadata_bytes, 3U);
    test_make_font_entry(metadata_bytes, bin_length, &entry, &view);
    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) == RESOURCE_PACK_FORMAT_ERROR);

    bin_length = test_build_font_bin(under, 3U, 1U);
    test_write_font_metadata(metadata_bytes, 3U);
    test_make_font_entry(metadata_bytes, bin_length, &entry, &view);
    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) == RESOURCE_PACK_OK);

    /* 空盒子字形（空格）的占位字节不计入位图总量。 */
    bin_length = test_build_font_bin(blank_glyphs, 5U, 1U);
    test_write_font_metadata(metadata_bytes, 5U);
    test_make_font_entry(metadata_bytes, bin_length, &entry, &view);
    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) == RESOURCE_PACK_OK);
}

/**
 * @brief 验证已入库的真实字体 bin 可按设计的 Metadata 解码，并拒绝与实测不符的字形数。
 */
static void test_decodes_committed_font_bin(void)
{
    uint8_t metadata_bytes[TEST_FONT_METADATA_SIZE];
    ResourcePack_EntryTypeDef entry;
    ResourcePack_EntryViewTypeDef view;
    ResourcePack_FontMetadataTypeDef metadata;
    FILE *file = fopen(TEST_FONT_BIN_PATH, "rb");
    size_t length;

    assert(file != NULL);
    length = fread(test_font_bin, 1U, sizeof(test_font_bin), file);
    assert(feof(file) != 0);
    assert(fclose(file) == 0);

    /* 7540 个字符加索引 0 的 notdef；字号 12，ascent 13，descent -3。 */
    test_write_font_metadata(metadata_bytes, 7541U);
    test_make_font_entry(metadata_bytes, (uint32_t)length, &entry, &view);
    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) == RESOURCE_PACK_OK);
    assert(metadata.GlyphCount == 7541U);

    test_write_font_metadata(metadata_bytes, 7540U);
    test_make_font_entry(metadata_bytes, (uint32_t)length, &entry, &view);
    assert(ResourcePack_DecodeFontMetadata(&entry, &view, &metadata) == RESOURCE_PACK_FORMAT_ERROR);
}

/**
 * @brief 验证分段定位给出真实字体 bin 的分段范围与 head 参数，并拒绝截断与非法参数。
 */
static void test_locates_committed_font_bin_sections(void)
{
    ResourcePack_FontBinLayoutTypeDef layout;
    FILE *file = fopen(TEST_FONT_BIN_PATH, "rb");
    size_t length;

    assert(file != NULL);
    length = fread(test_font_bin, 1U, sizeof(test_font_bin), file);
    assert(feof(file) != 0);
    assert(fclose(file) == 0);

    /* 分段范围与 head 参数由独立脚本逐段读出：head 48 B、cmap 13496 B、loca 30176 B、glyf 540328 B，其后是 kern。 */
    assert(ResourcePack_LocateFontBin(test_font_bin, (uint32_t)length, &layout) == RESOURCE_PACK_OK);
    assert((layout.Head.Offset == 0U) && (layout.Head.Length == 48U));
    assert((layout.Cmap.Offset == 48U) && (layout.Cmap.Length == 13496U));
    assert((layout.Loca.Offset == 13544U) && (layout.Loca.Length == 30176U));
    assert((layout.Glyf.Offset == 43720U) && (layout.Glyf.Length == 540328U));
    assert(layout.GlyphCount == 7541U);
    assert(layout.TablesCount == 4U);
    assert((layout.PixelHeight == 12U) && (layout.Ascent == 13) && (layout.Descent == -3));
    assert((layout.IndexToLocFormat == 1U) && (layout.GlyphIdFormat == 1U));
    assert((layout.AdvanceWidthFormat == 1U) && (layout.BitsPerPixel == 4U));
    assert((layout.XyBits == 5U) && (layout.WhBits == 4U) && (layout.AdvanceWidthBits == 9U));
    assert((layout.CompressionId == 0U) && (layout.SubpixelsMode == 0U));
    assert((layout.DefaultAdvanceWidth == 0U) && (layout.KerningScale == 16U));
    assert((layout.UnderlinePosition == 1) && (layout.UnderlineThickness == 0U));

    /* glyf 被截断时分段越界。 */
    assert(ResourcePack_LocateFontBin(test_font_bin, 43720U + 540328U - 1U, &layout) ==
           RESOURCE_PACK_FORMAT_ERROR);
    assert(ResourcePack_LocateFontBin(test_font_bin, 40U, &layout) == RESOURCE_PACK_FORMAT_ERROR);
    assert(ResourcePack_LocateFontBin(NULL, (uint32_t)length, &layout) == RESOURCE_PACK_INVALID_PARAM);
    assert(ResourcePack_LocateFontBin(test_font_bin, (uint32_t)length, NULL) == RESOURCE_PACK_INVALID_PARAM);
}

/**
 * @brief 运行全部 RPKC1 主机行为测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_open_find_and_decode_binary();
    test_open_and_decode_image();
    test_open_and_decode_font();
    test_decodes_both_loca_formats();
    test_font_rejects_wrong_metadata_size();
    test_font_rejects_unknown_enumerations();
    test_font_decoder_rejects_other_types();
    test_font_rejects_metadata_that_disagrees_with_bin();
    test_font_bitmap_total_limit();
    test_decodes_committed_font_bin();
    test_locates_committed_font_bin_sections();
    test_allows_unknown_type_at_core_level();
    test_rejects_corrupt_header();
    test_rejects_overlapping_regions();

    puts("resource_pack_component_tests: all tests passed");
    return 0;
}
