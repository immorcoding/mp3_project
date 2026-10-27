/**
 ******************************************************************************
 * @file    test_gui_font_decode_synthetic.c
 * @brief   字体 bin 解码器的小型构造样本测试：合法布局的逐字节期望，以及各类失败输入。
 *
 * 样本由 gui_font_test_support 的构造器按 lv_font_conv 的 font_spec 装配，期望值为手工推出的字面量，
 * 不由解码器的任何内部函数计算。
 ******************************************************************************
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Service/gui/font/gui_service_font_decoder.h"
#include "gui_font_test_support.h"

#define TEST_BIN_CAPACITY    (2U * 1024U * 1024U)
#define TEST_GUARD_BYTES     64U
#define TEST_GUARD_PATTERN   0xA5U

static uint8_t test_bin[TEST_BIN_CAPACITY];
static uint64_t test_region_storage[(TEST_BIN_CAPACITY + 4096U + TEST_GUARD_BYTES) / 8U];
static uint8_t test_cmap_payload[512];
static uint8_t test_kern_payload[512];

/** @brief 一次成功解码的现场：输出区、字体与规模。 */
typedef struct
{
    uint8_t *Region;
    uint32_t RegionSize;
    lv_font_t Font;
    Service_GUI_FontDecodeInfoTypeDef Info;
} TestDecoded;

/**
 * @brief 判断指针区间是否整体落在输出区内。
 * @param[in] decoded 解码现场。
 * @param[in] pointer 区间起点。
 * @param[in] length 区间字节数。
 * @return 落在区内返回 1。
 */
static int test_inside(const TestDecoded *decoded, const void *pointer, size_t length)
{
    const uint8_t *start = (const uint8_t *)pointer;

    return (start >= decoded->Region) && ((start + length) <= (decoded->Region + decoded->RegionSize));
}

/**
 * @brief 以恰好够用的输出区解码，并检查保护带未被写穿。
 * @param[in] bin bin 首地址。
 * @param[in] length bin 长度。
 * @param[out] decoded 接收现场。
 */
static void test_decode_ok(const uint8_t *bin, uint32_t length, TestDecoded *decoded)
{
    Service_GUI_FontDecodeInfoTypeDef measured;

    assert(service_gui_font_decode_measure(bin, length, &measured) == SERVICE_GUI_FONT_DECODE_OK);
    assert((measured.RegionSize % SERVICE_GUI_FONT_DECODE_REGION_ALIGN) == 0U);

    decoded->Region = (uint8_t *)test_region_storage;
    decoded->RegionSize = measured.RegionSize;
    memset(decoded->Region, TEST_GUARD_PATTERN, measured.RegionSize + TEST_GUARD_BYTES);
    memset(&decoded->Font, 0xFF, sizeof(decoded->Font));

    assert(service_gui_font_decode_build(bin, length, decoded->Region, decoded->RegionSize, &decoded->Font,
                                         &decoded->Info) == SERVICE_GUI_FONT_DECODE_OK);
    assert(memcmp(&measured, &decoded->Info, sizeof(measured)) == 0);

    for (uint32_t index = 0U; index < TEST_GUARD_BYTES; index++)
    {
        assert(decoded->Region[decoded->RegionSize + index] == TEST_GUARD_PATTERN);
    }
}

static const uint8_t test_a_bitmap_1[] = {0xABU, 0xCDU};
static const uint8_t test_a_bitmap_2[] = {0x12U, 0x34U, 0x56U, 0x78U, 0x90U};
static const uint8_t test_a_bitmap_4[] = {0xF0U};
static const uint8_t test_a_bitmap_5[] = {0xDEU, 0xADU, 0xBEU, 0xEFU};
static const uint16_t test_a_list_c2[] = {0x01U, 0x10U, 0xFFU};
static const uint16_t test_a_list_c3[] = {0x00U, 0xFFU};
static const uint16_t test_a_ofs_c1[] = {0U, 3U, 1U, 0U};
static const uint16_t test_a_ofs_c3[] = {5U, 2U};
static const GftHead test_a_head = {4U, 8U, 3U, 4U, 0U, 0U, 1U, 4U, 0U, 0U, 0U, 16U, 13U, -3};
static const GftGlyph test_a_glyphs[] = {
    {0U, 0, 0, 0U, 0U, NULL, 0U},
    {0x75U, 0, 0, 2U, 2U, test_a_bitmap_1, 16U},
    {0x70U, -2, 3, 3U, 3U, test_a_bitmap_2, 36U},
    {0x2BU, 0, 0, 0U, 0U, NULL, 0U},
    {0x80U, 3, -4, 1U, 1U, test_a_bitmap_4, 4U},
    {0xFFU, -4, 1, 4U, 2U, test_a_bitmap_5, 32U},
    {0x10U, 1, 1, 0U, 5U, NULL, 0U},
};

/**
 * @brief 取样本 A 的四张 cmap 子表。
 * @param[out] cmaps 至少 4 项。
 */
static void test_a_cmaps(GftCmap *cmaps)
{
    const GftCmap table[4] = {
        {0x41U, 3U, 1U, LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY, 3U, NULL, NULL},
        {0x100U, 4U, 1U, LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL, 4U, NULL, test_a_ofs_c1},
        {0x1000U, 0x100U, 4U, LV_FONT_FMT_TXT_CMAP_SPARSE_TINY, 3U, test_a_list_c2, NULL},
        {0x10FF00U, 0x100U, 0U, LV_FONT_FMT_TXT_CMAP_SPARSE_FULL, 2U, test_a_list_c3, test_a_ofs_c3},
    };

    memcpy(cmaps, table, sizeof(table));
}

/**
 * @brief 装配以样本 A 为蓝本的 bin，head、cmap、字形可被覆盖。
 *
 * kern 为格式 0、3 对、8 位字形编号：(1,2)=-2，(2,3)=5，(4,4)=-1。
 * @param[in] head head 参数。
 * @param[in] cmaps cmap 子表。
 * @param[in] cmap_count 子表数。
 * @param[in] glyphs 字形数组。
 * @param[in] glyph_count 字形数。
 * @param[out] layout 接收分段位置。
 * @return bin 长度。
 */
static uint32_t test_build_a_with(
    const GftHead *head,
    const GftCmap *cmaps,
    uint32_t cmap_count,
    const GftGlyph *glyphs,
    uint32_t glyph_count,
    GftBinLayout *layout)
{
    size_t cmap_size = gft_build_cmap_payload(test_cmap_payload, cmaps, cmap_count);

    memset(test_kern_payload, 0, sizeof(test_kern_payload));
    gft_put_u32(test_kern_payload + 4U, 3U);
    test_kern_payload[8] = 1U;
    test_kern_payload[9] = 2U;
    test_kern_payload[10] = 2U;
    test_kern_payload[11] = 3U;
    test_kern_payload[12] = 4U;
    test_kern_payload[13] = 4U;
    test_kern_payload[14] = (uint8_t)-2;
    test_kern_payload[15] = 5U;
    test_kern_payload[16] = (uint8_t)-1;

    *layout = gft_build_bin(test_bin, head, test_cmap_payload, cmap_size, glyphs, glyph_count, test_kern_payload, 17U);
    return layout->Length;
}

/**
 * @brief 样本 A：2 字节 loca、8 位字距对、非整字节字形头（22 位）、四种 cmap 子表。
 * @param[out] layout 接收分段位置。
 * @return bin 长度。
 */
static uint32_t test_build_sample_a(GftBinLayout *layout)
{
    GftCmap cmaps[4];

    test_a_cmaps(cmaps);
    return test_build_a_with(&test_a_head, cmaps, 4U, test_a_glyphs, 7U, layout);
}

/**
 * @brief 样本 A 解码后字形表、位图、cmap 与字距的逐项期望。
 */
static void test_sample_a_decodes_to_hand_derived_tables(void)
{
    static const uint8_t expected_bitmap[] = {
        0xABU, 0xCDU, 0x00U,                      /* 字形 1：38 位条目，位图自第 22 位起。 */
        0x12U, 0x34U, 0x56U, 0x78U, 0x90U, 0x00U, /* 字形 2：36 位位图补到 6 字节。 */
        0xF0U, 0x00U,                             /* 字形 4：4 位位图。 */
        0xDEU, 0xADU, 0xBEU, 0xEFU, 0x00U         /* 字形 5。 */
    };
    GftBinLayout layout;
    TestDecoded decoded;
    const lv_font_fmt_txt_dsc_t *dsc;
    uint32_t length = test_build_sample_a(&layout);

    test_decode_ok(test_bin, length, &decoded);

    assert(decoded.Info.GlyphCount == 7U);
    assert(decoded.Info.CmapCount == 4U);
    assert(decoded.Info.BitmapBytes == sizeof(expected_bitmap));
    assert(decoded.Info.KernPairCount == 3U);

    assert(decoded.Font.line_height == 16);
    assert(decoded.Font.base_line == 3);
    assert(decoded.Font.subpx == 0U);
    assert(decoded.Font.underline_position == -2);
    assert(decoded.Font.underline_thickness == 1);
    assert(decoded.Font.fallback == NULL);
    assert(decoded.Font.get_glyph_dsc == lv_font_get_glyph_dsc_fmt_txt);
    assert(decoded.Font.get_glyph_bitmap == lv_font_get_bitmap_fmt_txt);
    assert(test_inside(&decoded, decoded.Font.dsc, sizeof(lv_font_fmt_txt_dsc_t)));

    dsc = (const lv_font_fmt_txt_dsc_t *)decoded.Font.dsc;
    assert(dsc->bpp == 4U);
    assert(dsc->bitmap_format == LV_FONT_FMT_TXT_PLAIN);
    assert(dsc->cmap_num == 4U);
    assert(dsc->kern_scale == 16U);
    assert(dsc->kern_classes == 0U);
    assert(dsc->cache == NULL);

    assert(test_inside(&decoded, dsc->glyph_dsc, 7U * sizeof(lv_font_fmt_txt_glyph_dsc_t)));
    assert(test_inside(&decoded, dsc->glyph_bitmap, sizeof(expected_bitmap)));
    assert(test_inside(&decoded, dsc->cmaps, 4U * sizeof(lv_font_fmt_txt_cmap_t)));
    assert(memcmp(dsc->glyph_bitmap, expected_bitmap, sizeof(expected_bitmap)) == 0);

    {
        /* 期望：{bitmap_index, adv_w, box_w, box_h, ofs_x, ofs_y}；字形 0 全零，空盒子字形不占位图。 */
        static const struct
        {
            uint32_t BitmapIndex;
            uint32_t Adv;
            uint8_t W;
            uint8_t H;
            int8_t Ox;
            int8_t Oy;
        } expected[7] = {
            {0U, 0U, 0U, 0U, 0, 0},     {0U, 0x75U, 2U, 2U, 0, 0},  {3U, 0x70U, 3U, 3U, -2, 3},
            {9U, 0x2BU, 0U, 0U, 0, 0},  {9U, 0x80U, 1U, 1U, 3, -4}, {11U, 0xFFU, 4U, 2U, -4, 1},
            {16U, 0x10U, 0U, 5U, 1, 1},
        };

        for (uint32_t index = 0U; index < 7U; index++)
        {
            assert(dsc->glyph_dsc[index].bitmap_index == expected[index].BitmapIndex);
            assert(dsc->glyph_dsc[index].adv_w == expected[index].Adv);
            assert(dsc->glyph_dsc[index].box_w == expected[index].W);
            assert(dsc->glyph_dsc[index].box_h == expected[index].H);
            assert(dsc->glyph_dsc[index].ofs_x == expected[index].Ox);
            assert(dsc->glyph_dsc[index].ofs_y == expected[index].Oy);
        }
    }

    /* cmap：四种子表的命中。 */
    assert(gft_glyph_id(&decoded.Font, 0x41U) == 1U);
    assert(gft_glyph_id(&decoded.Font, 0x43U) == 3U);
    assert(gft_glyph_id(&decoded.Font, 0x100U) == 1U);
    assert(gft_glyph_id(&decoded.Font, 0x101U) == 4U);
    assert(gft_glyph_id(&decoded.Font, 0x102U) == 2U);
    assert(gft_glyph_id(&decoded.Font, 0x1001U) == 4U);
    assert(gft_glyph_id(&decoded.Font, 0x1010U) == 5U);
    assert(gft_glyph_id(&decoded.Font, 0x10FFU) == 6U);
    assert(gft_glyph_id(&decoded.Font, 0x10FF00U) == 5U);
    assert(gft_glyph_id(&decoded.Font, 0x10FFFFU) == 2U);

    /* cmap：范围之外与范围末尾的下一个码点（LVGL 的 rcp == range_length 上界）都必须得到 0。 */
    assert(gft_glyph_id(&decoded.Font, 0x40U) == 0U);
    assert(gft_glyph_id(&decoded.Font, 0x44U) == 0U);
    assert(gft_glyph_id(&decoded.Font, 0x104U) == 0U);
    assert(gft_glyph_id(&decoded.Font, 0x1000U) == 0U);
    assert(gft_glyph_id(&decoded.Font, 0x1100U) == 0U);
    assert(gft_glyph_id(&decoded.Font, 0x10FF01U) == 0U);
    assert(gft_glyph_id(&decoded.Font, 0x110000U) == 0U);

    /* kern：命中、反向无值。 */
    assert(gft_kern_value(&decoded.Font, 1U, 2U, 7U) == -2);
    assert(gft_kern_value(&decoded.Font, 2U, 3U, 7U) == 5);
    assert(gft_kern_value(&decoded.Font, 4U, 4U, 7U) == -1);
    assert(gft_kern_value(&decoded.Font, 2U, 1U, 7U) == 0);
    assert(test_inside(&decoded, ((const lv_font_fmt_txt_kern_pair_t *)dsc->kern_dsc)->glyph_ids, 6U));
    assert((((uintptr_t)((const lv_font_fmt_txt_kern_pair_t *)dsc->kern_dsc)->glyph_ids) % 4U) == 0U);
}

/**
 * @brief 样本 B：4 字节 loca、整字节字形头、默认前进宽度（整数像素）、2 bpp、类别字距，无 TablesCount 缺失。
 * @param[in] with_kern 为 0 时去掉 kern 分段并把 TablesCount 记为 3。
 * @param[out] layout 接收分段位置。
 * @return bin 长度。
 */
static uint32_t test_build_sample_b(int with_kern, GftBinLayout *layout)
{
    static const uint8_t bitmap_1[] = {0xB4U, 0xE0U};
    static const uint8_t bitmap_2[] = {0xC0U};
    static const uint16_t list_c1[] = {0U, 5U};
    static const uint16_t ofs_c1[] = {1U, 2U};
    GftHead head = {2U, 0U, 4U, 4U, 1U, 1U, 0U, 4U, 0U, 0U, 10U, 16U, 13U, -3};
    const GftGlyph glyphs[] = {
        {0U, 0, 0, 0U, 0U, NULL, 0U},
        {0U, 7, -8, 3U, 2U, bitmap_1, 12U},
        {0U, -1, 0, 1U, 1U, bitmap_2, 2U},
    };
    const GftCmap cmaps[] = {
        {0x20U, 1U, 1U, LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY, 1U, NULL, NULL},
        {0x4E00U, 0x10U, 0U, LV_FONT_FMT_TXT_CMAP_SPARSE_FULL, 2U, list_c1, ofs_c1},
    };
    size_t cmap_size = gft_build_cmap_payload(test_cmap_payload, cmaps, 2U);

    /* kern 格式 3：映射长度 2（短于字形数 3）、2 行 1 列；左类 {1,2}、右类 {1,1}、值 {-3,4}。 */
    memset(test_kern_payload, 0, sizeof(test_kern_payload));
    test_kern_payload[0] = 3U;
    gft_put_u16(test_kern_payload + 4U, 2U);
    test_kern_payload[6] = 2U;
    test_kern_payload[7] = 1U;
    test_kern_payload[8] = 1U;
    test_kern_payload[9] = 2U;
    test_kern_payload[10] = 1U;
    test_kern_payload[11] = 1U;
    test_kern_payload[12] = (uint8_t)-3;
    test_kern_payload[13] = 4U;

    if (!with_kern)
    {
        head.TablesCount = 3U;
    }

    *layout = gft_build_bin(test_bin, &head, test_cmap_payload, cmap_size, glyphs, 3U,
                            with_kern ? test_kern_payload : NULL, 14U);
    return layout->Length;
}

/**
 * @brief 样本 B 覆盖整字节快速路径、4 字节 loca、整数前进宽度换算与类别字距（映射补零到字形数）。
 */
static void test_sample_b_decodes_aligned_headers_and_kern_classes(void)
{
    static const uint8_t expected_bitmap[] = {0xB4U, 0xE0U, 0xC0U};
    GftBinLayout layout;
    TestDecoded decoded;
    const lv_font_fmt_txt_dsc_t *dsc;
    uint32_t length = test_build_sample_b(1, &layout);

    test_decode_ok(test_bin, length, &decoded);
    dsc = (const lv_font_fmt_txt_dsc_t *)decoded.Font.dsc;

    assert(decoded.Info.GlyphCount == 3U);
    assert(decoded.Info.CmapCount == 2U);
    assert(decoded.Info.BitmapBytes == sizeof(expected_bitmap));
    assert(decoded.Info.KernPairCount == 0U);
    assert(dsc->bpp == 2U);
    assert(memcmp(dsc->glyph_bitmap, expected_bitmap, sizeof(expected_bitmap)) == 0);

    /* 前进宽度取 head 默认值 10，整数格式乘 16。 */
    assert(dsc->glyph_dsc[1].adv_w == 160U);
    assert(dsc->glyph_dsc[2].adv_w == 160U);
    assert(dsc->glyph_dsc[1].ofs_x == 7);
    assert(dsc->glyph_dsc[1].ofs_y == -8);
    assert(dsc->glyph_dsc[2].ofs_x == -1);
    assert(dsc->glyph_dsc[1].bitmap_index == 0U);
    assert(dsc->glyph_dsc[2].bitmap_index == 2U);

    assert(gft_glyph_id(&decoded.Font, 0x20U) == 1U);
    assert(gft_glyph_id(&decoded.Font, 0x21U) == 0U);
    assert(gft_glyph_id(&decoded.Font, 0x4E00U) == 1U);
    assert(gft_glyph_id(&decoded.Font, 0x4E05U) == 2U);
    assert(gft_glyph_id(&decoded.Font, 0x4E10U) == 0U);

    assert(dsc->kern_classes == 1U);
    assert(gft_kern_value(&decoded.Font, 0U, 1U, 3U) == -3);
    assert(gft_kern_value(&decoded.Font, 1U, 1U, 3U) == 4);
    assert(gft_kern_value(&decoded.Font, 2U, 1U, 3U) == 0);
    assert(gft_kern_value(&decoded.Font, 1U, 2U, 3U) == 0);
}

/**
 * @brief 无 kern 分段（TablesCount 小于 4）时字体不带字距。
 */
static void test_font_without_kern_has_no_kern_descriptor(void)
{
    GftBinLayout layout;
    TestDecoded decoded;
    const lv_font_fmt_txt_dsc_t *dsc;
    uint32_t length = test_build_sample_b(0, &layout);

    test_decode_ok(test_bin, length, &decoded);
    dsc = (const lv_font_fmt_txt_dsc_t *)decoded.Font.dsc;
    assert(dsc->kern_dsc == NULL);
    assert(dsc->kern_scale == 0U);
    assert(dsc->kern_classes == 0U);
    assert(gft_kern_value(&decoded.Font, 1U, 1U, 3U) == 0);
}

/**
 * @brief 断言 Measure 与 Build 都以 expected 失败，且失败路径不写输出区、不改字体对象。
 * @param[in] length bin 长度。
 * @param[in] expected 期望的失败原因。
 */
static void test_expect_failure(uint32_t length, Service_GUI_FontDecodeStatusTypeDef expected)
{
    enum { CHECKED = 65536 };
    Service_GUI_FontDecodeInfoTypeDef info;
    lv_font_t font;
    uint8_t *region = (uint8_t *)test_region_storage;
    uint8_t untouched[sizeof(font)];

    memset(region, TEST_GUARD_PATTERN, CHECKED);
    memset(&font, 0xEE, sizeof(font));
    memcpy(untouched, &font, sizeof(font));

    assert(service_gui_font_decode_measure(test_bin, length, &info) == expected);
    assert(service_gui_font_decode_build(test_bin, length, region, sizeof(test_region_storage), &font, &info) ==
           expected);

    for (uint32_t index = 0U; index < CHECKED; index++)
    {
        assert(region[index] == TEST_GUARD_PATTERN);
    }
    assert(memcmp(&font, untouched, sizeof(font)) == 0);
}

/**
 * @brief 在样本 A 上改写若干字节后期望解码以 expected 失败。
 * @param[in] offset 改写位置。
 * @param[in] value 新值，按小端写 width 字节。
 * @param[in] width 写入宽度：1、2 或 4。
 * @param[in] expected 期望失败原因。
 */
static void test_tampered_sample_a(uint32_t offset, uint32_t value, uint32_t width, Service_GUI_FontDecodeStatusTypeDef expected)
{
    GftBinLayout layout;
    uint32_t length = test_build_sample_a(&layout);

    for (uint32_t index = 0U; index < width; index++)
    {
        test_bin[offset + index] = (uint8_t)(value >> (8U * index));
    }

    test_expect_failure(length, expected);
}

/**
 * @brief 任何前缀都不是合法字体：被截断的输入全部以格式错误失败，长度为 0 是参数错误。
 */
static void test_every_truncation_fails(void)
{
    GftBinLayout layout;
    uint32_t length;

    for (int variant = 0; variant < 3; variant++)
    {
        length = (variant == 0) ? test_build_sample_a(&layout) : test_build_sample_b(variant == 1, &layout);

        for (uint32_t cut = 1U; cut < length; cut++)
        {
            test_expect_failure(cut, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
        }

        test_expect_failure(0U, SERVICE_GUI_FONT_DECODE_INVALID_PARAM);
    }
}

/**
 * @brief 表长、偏移与字形头相互矛盾的输入以格式错误失败。
 */
static void test_inconsistent_tables_fail(void)
{
    GftBinLayout layout;
    uint32_t loca;
    uint32_t cmap_table;
    uint32_t kern;

    (void)test_build_sample_a(&layout);
    loca = layout.LocaOffset + 12U;
    cmap_table = layout.CmapOffset + 12U;
    kern = layout.KernOffset;

    /* loca 条目数多于 loca 分段容纳。 */
    test_tampered_sample_a(layout.LocaOffset + 8U, 8U, 4U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    /* loca 偏移倒退。 */
    test_tampered_sample_a(loca + (2U * 3U), 10U, 2U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    /* 最后一个字形的起点越过 glyf 分段。 */
    test_tampered_sample_a(loca + (2U * 6U), 0x4000U, 2U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    /* 字形 1 的起点落在分段前缀里。 */
    test_tampered_sample_a(loca + (2U * 1U), 4U, 2U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    /* 字形 1 的条目缩到比 22 位字形头还短（loca[1] = 8，loca[2] 改为 9）。 */
    test_tampered_sample_a(loca + (2U * 2U), 9U, 2U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    /* 字形 1 的起点落在分段前缀里，但 loca 仍单调（notdef 起点也改成 0）。 */
    {
        uint32_t length = test_build_sample_a(&layout);

        gft_put_u16(test_bin + loca + (2U * 0U), 0U);
        gft_put_u16(test_bin + loca + (2U * 1U), 4U);
        test_expect_failure(length, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    }
    /* cmap 子表数为 0 与大到装不下。 */
    test_tampered_sample_a(layout.CmapOffset + 8U, 0U, 4U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    test_tampered_sample_a(layout.CmapOffset + 8U, 5000U, 4U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    /* 子表数据偏移越过 cmap 分段，或落进子表表头。 */
    test_tampered_sample_a(cmap_table + 0U, 0x7000U, 4U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    test_tampered_sample_a(cmap_table + 0U, 20U, 4U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    /* FORMAT0_FULL 的条目数与范围长度不一致。 */
    test_tampered_sample_a(cmap_table + 16U + 12U, 3U, 2U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    /* 稀疏列表条目数超出 cmap 分段。 */
    test_tampered_sample_a(cmap_table + 32U + 12U, 1000U, 2U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    /* FORMAT0_TINY 的末尾映射到字形数之外（glyph_id_start = 5，范围长 3，字形只有 7 个）。 */
    test_tampered_sample_a(cmap_table + 10U, 5U, 2U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    /* SPARSE_TINY 的末项映射到字形数之外（glyph_id_start = 5，三项映射到 5、6、7，字形只有 7 个）。 */
    test_tampered_sample_a(cmap_table + 32U + 10U, 5U, 2U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    /* FORMAT0_FULL 的偏移列表映射到字形数之外（glyph_id_start 1 加偏移 200）。 */
    test_tampered_sample_a(cmap_table + 4U * 16U + 3U, 200U, 1U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    /* kern：字距对数超出分段，标签被改，格式未知。 */
    test_tampered_sample_a(kern + 12U, 100U, 4U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    test_tampered_sample_a(kern + 4U, 'x', 1U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    test_tampered_sample_a(kern + 8U, 7U, 1U, SERVICE_GUI_FONT_DECODE_UNSUPPORTED);
    /* head：xy 位宽为 0、前进宽度格式编号越界。 */
    test_tampered_sample_a(0x26U, 0U, 1U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
    test_tampered_sample_a(0x24U, 2U, 1U, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
}

/**
 * @brief 类别字距的映射项大于表的行数或列数时以格式错误失败，否则 LVGL 会越界读取值表。
 */
static void test_kern_class_index_beyond_table_fails(void)
{
    GftBinLayout layout;
    uint32_t length = test_build_sample_b(1, &layout);
    uint32_t left = layout.KernOffset + 16U;
    uint32_t right = left + 2U;

    /* 左映射 {1,2}：行数为 2，改成 3 越界。 */
    test_bin[left + 1U] = 3U;
    test_expect_failure(length, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);

    /* 右映射 {1,1}：列数为 1，改成 2 越界。 */
    length = test_build_sample_b(1, &layout);
    test_bin[right + 1U] = 2U;
    test_expect_failure(length, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
}

/**
 * @brief 稀疏列表不再严格升序时以格式错误失败。
 */
static void test_unsorted_sparse_list_fails(void)
{
    static const uint16_t unsorted[] = {0x10U, 0x01U};
    GftBinLayout layout;
    GftCmap cmaps[1];
    uint32_t length;

    cmaps[0] = (GftCmap){0x1000U, 0x100U, 1U, LV_FONT_FMT_TXT_CMAP_SPARSE_TINY, 2U, unsorted, NULL};
    length = test_build_a_with(&test_a_head, cmaps, 1U, test_a_glyphs, 7U, &layout);
    test_expect_failure(length, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
}

/**
 * @brief 位图比字形头声明的盒子小、前进宽度超过 12 位字段时以格式错误失败。
 */
static void test_glyph_fields_that_do_not_fit_are_rejected(void)
{
    static const uint8_t short_bitmap[] = {0x12U};
    GftBinLayout layout;
    GftGlyph glyphs[7];
    GftHead head = test_a_head;
    GftCmap cmaps[4];
    uint32_t length;

    test_a_cmaps(cmaps);

    /* 字形 2 声明 3x3 的 4 bpp 盒子（需要 36 位），条目里只有 8 位位图。 */
    memcpy(glyphs, test_a_glyphs, sizeof(glyphs));
    glyphs[2].BitmapBits = 8U;
    glyphs[2].Bitmap = short_bitmap;
    length = test_build_a_with(&head, cmaps, 4U, glyphs, 7U, &layout);
    test_expect_failure(length, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);

    /* 前进宽度位宽 13 位，值 0x1000 超过 LVGL 12 位字段。 */
    head.AdvBits = 13U;
    memcpy(glyphs, test_a_glyphs, sizeof(glyphs));
    glyphs[1].Adv = 0x1000U;
    length = test_build_a_with(&head, cmaps, 4U, glyphs, 7U, &layout);
    test_expect_failure(length, SERVICE_GUI_FONT_DECODE_FORMAT_ERROR);
}

/**
 * @brief 码点范围超出 Unicode，或稀疏列表项超出其范围时返回码点越界。
 */
static void test_codepoint_out_of_range_fails(void)
{
    static const uint16_t list_ok[] = {0x00U};
    static const uint16_t list_out[] = {0x00U, 0x10U};
    Service_GUI_FontDecodeInfoTypeDef info;
    GftBinLayout layout;
    GftCmap cmaps[1];
    uint32_t length;

    /* 起点本身越界。 */
    cmaps[0] = (GftCmap){0x110000U, 1U, 1U, LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY, 1U, NULL, NULL};
    length = test_build_a_with(&test_a_head, cmaps, 1U, test_a_glyphs, 7U, &layout);
    test_expect_failure(length, SERVICE_GUI_FONT_DECODE_CODEPOINT_RANGE);

    /* 起点合法，末尾越过 0x10FFFF。 */
    cmaps[0] = (GftCmap){0x10FFFFU, 2U, 1U, LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY, 2U, NULL, NULL};
    length = test_build_a_with(&test_a_head, cmaps, 1U, test_a_glyphs, 7U, &layout);
    test_expect_failure(length, SERVICE_GUI_FONT_DECODE_CODEPOINT_RANGE);

    /* 稀疏列表项 0x10 不小于范围长度 0x10。 */
    cmaps[0] = (GftCmap){0x4E00U, 0x10U, 1U, LV_FONT_FMT_TXT_CMAP_SPARSE_TINY, 2U, list_out, NULL};
    length = test_build_a_with(&test_a_head, cmaps, 1U, test_a_glyphs, 7U, &layout);
    test_expect_failure(length, SERVICE_GUI_FONT_DECODE_CODEPOINT_RANGE);

    /* 边界：末尾恰为 0x10FFFF 合法。 */
    cmaps[0] = (GftCmap){0x10FFFFU, 1U, 1U, LV_FONT_FMT_TXT_CMAP_SPARSE_TINY, 1U, list_ok, NULL};
    length = test_build_a_with(&test_a_head, cmaps, 1U, test_a_glyphs, 7U, &layout);
    assert(service_gui_font_decode_measure(test_bin, length, &info) == SERVICE_GUI_FONT_DECODE_OK);
}

/**
 * @brief 把若干 8 bpp 字形装成 bin，用来逼近 1 MiB 位图上限。
 * @param[in] widths 各字形的盒子宽。
 * @param[in] heights 各字形的盒子高。
 * @param[in] count 字形数（不含 notdef）。
 * @param[out] layout 接收分段位置。
 * @return bin 长度。
 */
static uint32_t test_build_large_bitmap_font(const uint8_t *widths, const uint8_t *heights, uint32_t count, GftBinLayout *layout)
{
    static const GftHead head = {8U, 8U, 2U, 8U, 1U, 1U, 1U, 3U, 0U, 0U, 0U, 16U, 13U, -3};
    static const uint16_t list[] = {0U};
    static const uint8_t zeros[255 * 255];
    static GftGlyph glyphs[40];
    GftCmap cmap = {0x4E00U, 1U, 1U, LV_FONT_FMT_TXT_CMAP_SPARSE_TINY, 1U, list, NULL};
    size_t cmap_size = gft_build_cmap_payload(test_cmap_payload, &cmap, 1U);

    memset(glyphs, 0, sizeof(glyphs));
    for (uint32_t index = 0U; index < count; index++)
    {
        glyphs[index + 1U].Adv = 100U;
        glyphs[index + 1U].W = widths[index];
        glyphs[index + 1U].H = heights[index];
        glyphs[index + 1U].Bitmap = zeros;
        glyphs[index + 1U].BitmapBits = 8U * widths[index] * heights[index];
    }

    *layout = gft_build_bin(test_bin, &head, test_cmap_payload, cmap_size, glyphs, count + 1U, NULL, 0U);
    return layout->Length;
}

/**
 * @brief 位图总量 1048575 B 时通过，恰为 1 MiB 时以位图过大失败。
 */
static void test_bitmap_total_limit_is_one_mebibyte(void)
{
    /* 字形头 28 位，位图自第 28 位起，每个字形在条目里存「宽*高 + 1」B。
     * 16 个 255x255 共 1040416 B；255x31 存 7906 B；252x1 存 253 B，合计 1048575；253x1 存 254 B，合计 1048576。 */
    uint8_t widths[18];
    uint8_t heights[18];
    GftBinLayout layout;
    TestDecoded decoded;
    uint32_t length;

    for (int index = 0; index < 16; index++)
    {
        widths[index] = 255U;
        heights[index] = 255U;
    }
    widths[16] = 255U;
    heights[16] = 31U;
    widths[17] = 252U;
    heights[17] = 1U;

    length = test_build_large_bitmap_font(widths, heights, 18U, &layout);
    test_decode_ok(test_bin, length, &decoded);
    assert(decoded.Info.BitmapBytes == 1048575U);

    widths[17] = 253U;
    length = test_build_large_bitmap_font(widths, heights, 18U, &layout);
    test_expect_failure(length, SERVICE_GUI_FONT_DECODE_BITMAP_TOO_LARGE);
}

/**
 * @brief 超出本解码器范围的合法 bin 返回不支持：压缩位图、子像素、非常用 bpp、未知 cmap 类型。
 */
static void test_unsupported_variants_are_reported(void)
{
    GftBinLayout layout;
    uint32_t cmap_table;

    (void)test_build_sample_a(&layout);
    cmap_table = layout.CmapOffset + 12U;

    test_tampered_sample_a(0x29U, 1U, 1U, SERVICE_GUI_FONT_DECODE_UNSUPPORTED);
    test_tampered_sample_a(0x2AU, 1U, 1U, SERVICE_GUI_FONT_DECODE_UNSUPPORTED);
    test_tampered_sample_a(0x25U, 5U, 1U, SERVICE_GUI_FONT_DECODE_UNSUPPORTED);
    test_tampered_sample_a(cmap_table + 14U, 9U, 1U, SERVICE_GUI_FONT_DECODE_UNSUPPORTED);
}

/**
 * @brief 输出区不足、空指针与未对齐都在写入前被拒绝。
 */
static void test_output_region_is_checked_before_any_write(void)
{
    GftBinLayout layout;
    Service_GUI_FontDecodeInfoTypeDef info;
    lv_font_t font;
    uint8_t *region = (uint8_t *)test_region_storage;
    uint32_t length = test_build_sample_a(&layout);
    uint8_t copy[4096];

    assert(service_gui_font_decode_measure(test_bin, length, &info) == SERVICE_GUI_FONT_DECODE_OK);
    assert(info.RegionSize < sizeof(copy));

    /* 少一个对齐单位、少一字节、为 0：容量不足，区内一字节不动。 */
    {
        const uint32_t sizes[] = {info.RegionSize - SERVICE_GUI_FONT_DECODE_REGION_ALIGN, info.RegionSize - 1U, 0U};

        for (uint32_t index = 0U; index < 3U; index++)
        {
            memset(region, TEST_GUARD_PATTERN, sizeof(copy));
            memcpy(copy, region, sizeof(copy));
            memset(&font, 0xEE, sizeof(font));
            assert(service_gui_font_decode_build(test_bin, length, region, sizes[index], &font, NULL) ==
                   SERVICE_GUI_FONT_DECODE_OUTPUT_TOO_SMALL);
            assert(memcmp(region, copy, sizeof(copy)) == 0);
            assert(((const uint8_t *)&font)[0] == 0xEEU);
        }
    }

    /* 恰好够用则成功，info 可省略。 */
    assert(service_gui_font_decode_build(test_bin, length, region, info.RegionSize, &font, NULL) ==
           SERVICE_GUI_FONT_DECODE_OK);

    assert(service_gui_font_decode_build(NULL, length, region, info.RegionSize, &font, NULL) ==
           SERVICE_GUI_FONT_DECODE_INVALID_PARAM);
    assert(service_gui_font_decode_build(test_bin, length, NULL, info.RegionSize, &font, NULL) ==
           SERVICE_GUI_FONT_DECODE_INVALID_PARAM);
    assert(service_gui_font_decode_build(test_bin, length, region, info.RegionSize, NULL, NULL) ==
           SERVICE_GUI_FONT_DECODE_INVALID_PARAM);
    assert(service_gui_font_decode_build(test_bin, length, region + 4U, info.RegionSize, &font, NULL) ==
           SERVICE_GUI_FONT_DECODE_INVALID_PARAM);
    assert(service_gui_font_decode_measure(test_bin, length, NULL) == SERVICE_GUI_FONT_DECODE_INVALID_PARAM);
    assert(service_gui_font_decode_measure(NULL, length, &info) == SERVICE_GUI_FONT_DECODE_INVALID_PARAM);
}

/**
 * @brief 解码不修改输入 bin；每个解码结果都有非空文本。
 */
static void test_input_is_read_only_and_statuses_have_text(void)
{
    static uint8_t snapshot[TEST_BIN_CAPACITY];
    GftBinLayout layout;
    TestDecoded decoded;
    uint32_t length = test_build_sample_a(&layout);

    memcpy(snapshot, test_bin, length);
    test_decode_ok(test_bin, length, &decoded);
    assert(memcmp(snapshot, test_bin, length) == 0);

    for (int status = SERVICE_GUI_FONT_DECODE_OK; status <= SERVICE_GUI_FONT_DECODE_OUTPUT_TOO_SMALL; status++)
    {
        const char *text = service_gui_font_decode_status_text((Service_GUI_FontDecodeStatusTypeDef)status);

        assert((text != NULL) && (text[0] != '\0') && (strcmp(text, "unknown") != 0));
    }
    assert(strcmp(service_gui_font_decode_status_text((Service_GUI_FontDecodeStatusTypeDef)99), "unknown") == 0);
}

/**
 * @brief 运行全部构造样本测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_sample_a_decodes_to_hand_derived_tables();
    test_sample_b_decodes_aligned_headers_and_kern_classes();
    test_font_without_kern_has_no_kern_descriptor();
    test_every_truncation_fails();
    test_inconsistent_tables_fail();
    test_unsorted_sparse_list_fails();
    test_kern_class_index_beyond_table_fails();
    test_glyph_fields_that_do_not_fit_are_rejected();
    test_codepoint_out_of_range_fails();
    test_bitmap_total_limit_is_one_mebibyte();
    test_unsupported_variants_are_reported();
    test_output_region_is_checked_before_any_write();
    test_input_is_read_only_and_statuses_have_text();

    puts("gui_font_decode_synthetic_tests: all tests passed");
    return 0;
}
