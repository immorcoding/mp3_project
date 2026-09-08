/**
 ******************************************************************************
 * @file    lvgl.h
 * @brief   主机测试用的最小 LVGL 图片描述符替身。
 ******************************************************************************
 */

#ifndef LVGL_H
#define LVGL_H

#include <stdint.h>

typedef int16_t lv_coord_t;

typedef struct
{
    lv_coord_t x1;
    lv_coord_t y1;
    lv_coord_t x2;
    lv_coord_t y2;
} lv_area_t;

enum
{
    LV_IMG_CF_UNKNOWN = 0,
    LV_IMG_CF_RAW,
    LV_IMG_CF_RAW_ALPHA,
    LV_IMG_CF_RAW_CHROMA_KEYED,
    LV_IMG_CF_TRUE_COLOR,
    LV_IMG_CF_TRUE_COLOR_ALPHA
};

typedef uint8_t lv_img_cf_t;

typedef struct
{
    uint32_t cf : 5;
    uint32_t always_zero : 3;
    uint32_t reserved : 2;
    uint32_t w : 11;
    uint32_t h : 11;
} lv_img_header_t;

typedef struct
{
    lv_img_header_t header;
    uint32_t data_size;
    const uint8_t *data;
} lv_img_dsc_t;

uint8_t lv_img_cf_get_px_size(lv_img_cf_t cf);

#endif /* LVGL_H */
