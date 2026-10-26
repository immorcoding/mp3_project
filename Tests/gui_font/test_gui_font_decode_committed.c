/**
 ******************************************************************************
 * @file    test_gui_font_decode_committed.c
 * @brief   用已入库的真实字体 bin 验证解码结果与独立参照一致。
 *
 * 期望值来自独立脚本对同一份 bin 的解析（按位串整体移位重排位图、按 font_spec 的精确语义建立码点表，
 * 不共用解码器的任何代码），脚本输出的字面量写在本文件；脚本未入库，口径见模块 README。
 ******************************************************************************
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Service/gui/font/gui_service_font_decoder.h"
#include "gui_font_test_support.h"

#define TEST_FONT_BIN_CAPACITY  (1024U * 1024U)
#define TEST_GUARD_BYTES        64U
#define TEST_GUARD_PATTERN      0xA5U

static uint8_t test_bin[TEST_FONT_BIN_CAPACITY];
static uint64_t test_region_storage[(2U * 1024U * 1024U) / 8U];

/**
 * @brief 把真实字体 bin 读入 test_bin。
 * @return bin 长度。
 */
static uint32_t test_load_font(void)
{
    FILE *file = fopen(TEST_FONT_BIN_PATH, "rb");
    size_t length;

    assert(file != NULL);
    length = fread(test_bin, 1U, sizeof(test_bin), file);
    assert(feof(file) != 0);
    assert(fclose(file) == 0);
    return (uint32_t)length;
}

/**
 * @brief 把字形表按小端字段序列化后算 CRC，口径同独立脚本的 struct.pack('<HBBbbI')。
 * @param[in] dsc 解码得到的字体描述。
 * @param[in] glyph_count 字形数。
 * @return CRC-32。
 */
static uint32_t test_glyph_table_crc(const lv_font_fmt_txt_dsc_t *dsc, uint32_t glyph_count)
{
    uint32_t crc = 0U;

    for (uint32_t index = 0U; index < glyph_count; index++)
    {
        uint8_t packed[10];

        gft_put_u16(packed, (uint16_t)dsc->glyph_dsc[index].adv_w);
        packed[2] = dsc->glyph_dsc[index].box_w;
        packed[3] = dsc->glyph_dsc[index].box_h;
        packed[4] = (uint8_t)dsc->glyph_dsc[index].ofs_x;
        packed[5] = (uint8_t)dsc->glyph_dsc[index].ofs_y;
        gft_put_u32(packed + 6U, dsc->glyph_dsc[index].bitmap_index);
        crc = gft_crc32_update(crc, packed, sizeof(packed));
    }

    return crc;
}

/**
 * @brief 比较某个码点的字形：编号、字形头与位图前 exact_length 字节。
 * @param[in] font 解码得到的字体。
 * @param[in] letter 码点。
 * @param[in] glyph_id 期望字形编号。
 * @param[in] adv 期望前进宽度（12.4）。
 * @param[in] box_w 期望盒宽。
 * @param[in] box_h 期望盒高。
 * @param[in] ofs_x 期望 x 偏移。
 * @param[in] ofs_y 期望 y 偏移。
 * @param[in] bitmap_index 期望位图索引。
 * @param[in] bitmap 期望位图字节。
 * @param[in] bitmap_length 期望位图比较长度。
 */
static void test_expect_glyph(
    const lv_font_t *font,
    uint32_t letter,
    uint32_t glyph_id,
    uint32_t adv,
    uint8_t box_w,
    uint8_t box_h,
    int8_t ofs_x,
    int8_t ofs_y,
    uint32_t bitmap_index,
    const uint8_t *bitmap,
    size_t bitmap_length)
{
    const lv_font_fmt_txt_dsc_t *dsc = (const lv_font_fmt_txt_dsc_t *)font->dsc;
    const lv_font_fmt_txt_glyph_dsc_t *glyph;

    assert(gft_glyph_id(font, letter) == glyph_id);
    glyph = &dsc->glyph_dsc[glyph_id];
    assert(glyph->adv_w == adv);
    assert(glyph->box_w == box_w);
    assert(glyph->box_h == box_h);
    assert(glyph->ofs_x == ofs_x);
    assert(glyph->ofs_y == ofs_y);
    assert(glyph->bitmap_index == bitmap_index);
    assert(memcmp(dsc->glyph_bitmap + bitmap_index, bitmap, bitmap_length) == 0);
}

/**
 * @brief 解码真实字体：规模、整表校验和、抽样字形与字距与独立参照一致，且恰好够用的输出区不被写穿。
 */
static void test_committed_font_decodes_to_reference_tables(void)
{
    /* 'A'、'中'、'，' 的位图前 exact 字节（4 bpp，盒子面积 * 4 / 8 向上取整）。 */
    static const uint8_t bitmap_a[] = {
        0x00U, 0x2FU, 0x60U, 0x00U, 0x00U, 0x79U, 0xC0U, 0x00U, 0x00U, 0xC1U, 0xD1U, 0x00U,
        0x02U, 0xC0U, 0x86U, 0x00U, 0x07U, 0x70U, 0x3CU, 0x00U, 0x0CU, 0xEEU, 0xEFU, 0x10U,
        0x2DU, 0x00U, 0x09U, 0x60U, 0x79U, 0x00U, 0x05U, 0xC0U, 0xC4U, 0x00U, 0x01U, 0xF1U};
    static const uint8_t bitmap_zhong[] = {
        0x00U, 0x00U, 0xC2U, 0x00U, 0x00U, 0x00U, 0x00U, 0xC2U, 0x00U, 0x00U, 0xDDU, 0xDDU, 0xFEU, 0xDDU,
        0xE4U, 0xE0U, 0x00U, 0xC2U, 0x00U, 0xA4U, 0xE0U, 0x00U, 0xC2U, 0x00U, 0xA4U, 0xE0U, 0x00U, 0xC2U,
        0x00U, 0xA4U, 0xEEU, 0xEEU, 0xFEU, 0xEEU, 0xF4U, 0xA0U, 0x00U, 0xC2U, 0x00U, 0x73U, 0x00U, 0x00U,
        0xC2U, 0x00U, 0x00U, 0x00U, 0x00U, 0xC2U, 0x00U, 0x00U, 0x00U, 0x00U, 0xC2U, 0x00U, 0x00U};
    static const uint8_t bitmap_comma[] = {0x09U, 0x70U, 0xAEU, 0x02U, 0xB2U, 0xB2U, 0x00U, 0x00U};
    Service_GUI_FontDecodeInfoTypeDef info;
    lv_font_t font;
    const lv_font_fmt_txt_dsc_t *dsc;
    uint8_t *region = (uint8_t *)test_region_storage;
    uint32_t length = test_load_font();
    uint32_t lookup_crc = 0U;

    assert(service_gui_font_decode_measure(test_bin, length, &info) == SERVICE_GUI_FONT_DECODE_OK);
    assert(info.GlyphCount == 7541U); /* 7540 个字符加索引 0 的 notdef。 */
    assert(info.CmapCount == 55U);
    assert(info.BitmapBytes == 517698U);
    assert(info.KernPairCount == 4488U);
    {
        /* 规模下限由各表的原始字节数相加：描述头、55 个 cmap 条目、列表 17336 B（见 README 的口径；FORMAT0 空位不入列表）、
         * 7541 个字形描述、位图、kern（4488 对 * 4 B 的编号加 4488 B 的值）；每一块按 8 B 取整，余量不超过 9 块 * 7 B。 */
        uint32_t lower = (uint32_t)(sizeof(lv_font_fmt_txt_dsc_t) + (55U * sizeof(lv_font_fmt_txt_cmap_t)) + 17336U +
                                    (7541U * sizeof(lv_font_fmt_txt_glyph_dsc_t)) + 517698U +
                                    sizeof(lv_font_fmt_txt_kern_pair_t) + (4488U * 4U) + 4488U);

        assert(info.RegionSize >= lower);
        assert(info.RegionSize <= (lower + 63U));
    }
    printf("gui_font committed font: region = %u B\n", (unsigned)info.RegionSize);

    memset(region, TEST_GUARD_PATTERN, info.RegionSize + TEST_GUARD_BYTES);
    assert(service_gui_font_decode_build(test_bin, length, region, info.RegionSize, &font, NULL) ==
           SERVICE_GUI_FONT_DECODE_OK);
    for (uint32_t index = 0U; index < TEST_GUARD_BYTES; index++)
    {
        assert(region[info.RegionSize + index] == TEST_GUARD_PATTERN);
    }

    dsc = (const lv_font_fmt_txt_dsc_t *)font.dsc;
    assert(font.line_height == 16);
    assert(font.base_line == 3);
    assert(dsc->bpp == 4U);
    assert(dsc->kern_scale == 16U);
    assert(dsc->kern_classes == 0U);
    assert(dsc->cmap_num == 55U);

    /* 整表：字形表与重排后的位图总量与独立脚本逐字节一致。 */
    assert(test_glyph_table_crc(dsc, info.GlyphCount) == 0xE4EBCAA1U);
    assert(gft_crc32(dsc->glyph_bitmap, info.BitmapBytes) == 0x6EF52936U);

    /* 整个基本平面的码点表：能查到字形的码点恰为生成脚本的字集（GB2312 区位字符加 ASCII，共 7540 个），FORMAT0 空位落空；
     * 期望 CRC 来自独立脚本（按 bin 逐子表建表并与字集逐一核对）；查找用 LVGL 的写法（含 rcp == range_length）。 */
    for (uint32_t letter = 0U; letter < 0x10000U; letter++)
    {
        uint8_t packed[2];

        gft_put_u16(packed, (uint16_t)gft_glyph_id(&font, letter));
        lookup_crc = gft_crc32_update(lookup_crc, packed, sizeof(packed));
    }
    assert(lookup_crc == 0x0D974AB2U);

    /* 抽样：ASCII、希腊字母、假名、汉字、全角符号。 */
    assert(gft_glyph_id(&font, 0x20U) == 1U);
    test_expect_glyph(&font, 0x41U, 34U, 117U, 8U, 9U, 0, 0, 796U, bitmap_a, sizeof(bitmap_a));
    test_expect_glyph(&font, 0x56U, 55U, 110U, 7U, 9U, 0, 0, 1482U, bitmap_a, 0U);
    test_expect_glyph(&font, 0x3A9U, 154U, 146U, 9U, 9U, 0, 0, 4401U, bitmap_a, 0U);
    test_expect_glyph(&font, 0x3042U, 464U, 192U, 10U, 12U, 1, -1, 17860U, bitmap_a, 0U);
    test_expect_glyph(&font, 0x4E2DU, 712U, 192U, 10U, 11U, 1, -1, 32032U, bitmap_zhong, sizeof(bitmap_zhong));
    test_expect_glyph(&font, 0x6674U, 3059U, 192U, 11U, 11U, 1, -1, 197103U, bitmap_a, 0U);
    test_expect_glyph(&font, 0xFF0CU, 7454U, 192U, 3U, 5U, 1, -2, 514694U, bitmap_comma, sizeof(bitmap_comma));
    test_expect_glyph(&font, 0xFFE5U, 7540U, 192U, 8U, 9U, 2, 0, 517658U, bitmap_a, 0U);

    /* 不在字集中的码点：DEL 紧随 ASCII 范围，U+FFE6 紧随最后一个范围，U+9F9C 是字集外的汉字；全部得到 0 而非邻近字形。 */
    assert(gft_glyph_id(&font, 0x7FU) == 0U);
    assert(gft_glyph_id(&font, 0xFFE6U) == 0U);
    assert(gft_glyph_id(&font, 0x9F9CU) == 0U);
    assert(gft_glyph_id(&font, 0x10000U) == 0U);

    /*
     * FORMAT0 子表内部的空位：U+951D–U+957F 是一张带空位的 FORMAT0_FULL 子表，镕（U+9555）等 21 个
     * 码点不在 GB2312，bin 里它们的偏移与范围首字锝（U+951D）一样是 0。它们必须得到 0（占位框），
     * 而不是锝的字形；锝本身仍可查到。
     */
    assert(gft_glyph_id(&font, 0x9555U) == 0U);
    assert(gft_glyph_id(&font, 0x9520U) == 0U);
    assert(gft_glyph_id(&font, 0x957EU) == 0U);
    assert(gft_glyph_id(&font, 0x951DU) != 0U);

    /* kern：独立脚本对 (A,V)、(A,A) 的字距对。 */
    assert(gft_kern_value(&font, 34U, 55U, info.GlyphCount) == -3);
    assert(gft_kern_value(&font, 34U, 34U, info.GlyphCount) == -1);
}

/**
 * @brief 运行真实字体解码测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_committed_font_decodes_to_reference_tables();

    puts("gui_font_decode_committed_tests: all tests passed");
    return 0;
}
