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

/**
 * @brief 运行全部 RPKC1 主机行为测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_open_find_and_decode_binary();
    test_open_and_decode_image();
    test_allows_unknown_type_at_core_level();
    test_rejects_corrupt_header();
    test_rejects_overlapping_regions();

    puts("resource_pack_component_tests: all tests passed");
    return 0;
}
