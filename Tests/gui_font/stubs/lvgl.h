/**
 ******************************************************************************
 * @file    lvgl.h
 * @brief   主机测试用的最小 LVGL 字体类型替身。
 *
 * 字段与顺序照抄 LVGL v8.3.11 的 lv_font.h 与 lv_font_fmt_txt.h，条件编译固定为本工程
 * lv_conf.h 的取值（LV_FONT_FMT_TXT_LARGE = 0、LV_USE_USER_DATA = 1）。只提供解码器写入
 * 的数据结构，不含任何 LVGL 行为；lv_font_get_*_fmt_txt 两个回调只是占位符号。
 ******************************************************************************
 */

#ifndef LVGL_H
#define LVGL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_USER_DATA      1

typedef int16_t lv_coord_t;

typedef struct
{
    const struct _lv_font_t *resolved_font;
    uint16_t adv_w;
    uint16_t box_w;
    uint16_t box_h;
    int16_t ofs_x;
    int16_t ofs_y;
    uint8_t bpp : 4;
    uint8_t is_placeholder : 1;
} lv_font_glyph_dsc_t;

enum
{
    LV_FONT_SUBPX_NONE,
    LV_FONT_SUBPX_HOR,
    LV_FONT_SUBPX_VER,
    LV_FONT_SUBPX_BOTH
};

typedef uint8_t lv_font_subpx_t;

typedef struct _lv_font_t
{
    bool (*get_glyph_dsc)(const struct _lv_font_t *, lv_font_glyph_dsc_t *, uint32_t letter, uint32_t letter_next);
    const uint8_t *(*get_glyph_bitmap)(const struct _lv_font_t *, uint32_t);
    lv_coord_t line_height;
    lv_coord_t base_line;
    uint8_t subpx : 2;
    int8_t underline_position;
    int8_t underline_thickness;
    const void *dsc;
    const struct _lv_font_t *fallback;
#if LV_USE_USER_DATA
    void *user_data;
#endif
} lv_font_t;

typedef struct
{
    uint32_t bitmap_index : 20;
    uint32_t adv_w : 12;
    uint8_t box_w;
    uint8_t box_h;
    int8_t ofs_x;
    int8_t ofs_y;
} lv_font_fmt_txt_glyph_dsc_t;

enum
{
    LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL,
    LV_FONT_FMT_TXT_CMAP_SPARSE_FULL,
    LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
    LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
};

typedef uint8_t lv_font_fmt_txt_cmap_type_t;

typedef struct
{
    uint32_t range_start;
    uint16_t range_length;
    uint16_t glyph_id_start;
    const uint16_t *unicode_list;
    const void *glyph_id_ofs_list;
    uint16_t list_length;
    lv_font_fmt_txt_cmap_type_t type;
} lv_font_fmt_txt_cmap_t;

typedef struct
{
    const void *glyph_ids;
    const int8_t *values;
    uint32_t pair_cnt : 30;
    uint32_t glyph_ids_size : 2;
} lv_font_fmt_txt_kern_pair_t;

typedef struct
{
    const int8_t *class_pair_values;
    const uint8_t *left_class_mapping;
    const uint8_t *right_class_mapping;
    uint8_t left_class_cnt;
    uint8_t right_class_cnt;
} lv_font_fmt_txt_kern_classes_t;

typedef enum
{
    LV_FONT_FMT_TXT_PLAIN = 0,
    LV_FONT_FMT_TXT_COMPRESSED = 1,
    LV_FONT_FMT_TXT_COMPRESSED_NO_PREFILTER = 1
} lv_font_fmt_txt_bitmap_format_t;

typedef struct
{
    uint32_t last_letter;
    uint32_t last_glyph_id;
} lv_font_fmt_txt_glyph_cache_t;

typedef struct
{
    const uint8_t *glyph_bitmap;
    const lv_font_fmt_txt_glyph_dsc_t *glyph_dsc;
    const lv_font_fmt_txt_cmap_t *cmaps;
    const void *kern_dsc;
    uint16_t kern_scale;
    uint16_t cmap_num : 9;
    uint16_t bpp : 4;
    uint16_t kern_classes : 1;
    uint16_t bitmap_format : 2;
    lv_font_fmt_txt_glyph_cache_t *cache;
} lv_font_fmt_txt_dsc_t;

bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out, uint32_t unicode_letter,
                                   uint32_t unicode_letter_next);
const uint8_t *lv_font_get_bitmap_fmt_txt(const lv_font_t *font, uint32_t letter);

#endif /* LVGL_H */
