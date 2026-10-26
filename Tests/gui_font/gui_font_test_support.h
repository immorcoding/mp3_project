/**
 ******************************************************************************
 * @file    gui_font_test_support.h
 * @brief   gui_font 主机测试的共用工具：lv_font_conv bin 构造器与 LVGL 查找语义的独立复刻。
 ******************************************************************************
 */

#ifndef GUI_FONT_TEST_SUPPORT_H
#define GUI_FONT_TEST_SUPPORT_H

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

/** @brief 构造 bin 的 head 参数；字段含义同 lv_font_conv 的 font_spec。 */
typedef struct
{
    uint8_t Bpp;
    uint8_t AdvBits;
    uint8_t XyBits;
    uint8_t WhBits;
    uint8_t LocFormat;
    uint8_t GlyphIdFormat;
    uint8_t AdvFormat;
    uint8_t TablesCount;
    uint8_t Compression;
    uint8_t Subpx;
    uint16_t DefaultAdvance;
    uint16_t KernScale;
    uint16_t Ascent;
    int16_t Descent;
} GftHead;

/** @brief 一个字形条目：字形头字段加按 MSB 优先排列的位图位流。 */
typedef struct
{
    uint16_t Adv;
    int16_t Ox;
    int16_t Oy;
    uint16_t W;
    uint16_t H;
    const uint8_t *Bitmap;
    uint32_t BitmapBits;
} GftGlyph;

/** @brief 一个 cmap 子表；Ofs 对 FULL 类型有效。 */
typedef struct
{
    uint32_t RangeStart;
    uint16_t RangeLength;
    uint16_t GlyphIdStart;
    uint8_t Type;
    uint16_t Count;
    const uint16_t *List;
    const uint16_t *Ofs;
} GftCmap;

/** @brief 构造结果：各分段在 bin 内的起点，便于测试定点篡改。 */
typedef struct
{
    uint32_t Length;
    uint32_t CmapOffset;
    uint32_t LocaOffset;
    uint32_t GlyfOffset;
    uint32_t KernOffset; /**< 无 kern 时等于 Length。 */
} GftBinLayout;

uint32_t gft_crc32(const void *data, size_t length);
uint32_t gft_crc32_update(uint32_t crc, const void *data, size_t length);

size_t gft_build_cmap_payload(uint8_t *out, const GftCmap *cmaps, uint32_t count);
GftBinLayout gft_build_bin(
    uint8_t *out,
    const GftHead *head,
    const uint8_t *cmap_payload,
    size_t cmap_payload_size,
    const GftGlyph *glyphs,
    uint32_t glyph_count,
    const uint8_t *kern_payload,
    size_t kern_payload_size);

void gft_put_u16(uint8_t *destination, uint16_t value);
void gft_put_u32(uint8_t *destination, uint32_t value);

uint32_t gft_glyph_id(const lv_font_t *font, uint32_t letter);
int gft_kern_value(const lv_font_t *font, uint32_t gid_left, uint32_t gid_right, uint32_t glyph_count);

#endif /* GUI_FONT_TEST_SUPPORT_H */
