#ifndef LVGL_H
#define LVGL_H

#include <stdint.h>

typedef int16_t lv_coord_t;

typedef enum
{
    LV_IMG_CF_UNKNOWN = 0,
    LV_IMG_CF_TRUE_COLOR,
    LV_IMG_CF_TRUE_COLOR_ALPHA
} lv_img_cf_t;

static inline uint8_t lv_img_cf_get_px_size(lv_img_cf_t color_format)
{
    if (color_format == LV_IMG_CF_TRUE_COLOR)
    {
        return 16U;
    }

    return (color_format == LV_IMG_CF_TRUE_COLOR_ALPHA) ? 24U : 0U;
}

typedef struct
{
    uint32_t cf;
    lv_coord_t w;
    lv_coord_t h;
} lv_img_header_t;

typedef struct
{
    lv_img_header_t header;
    uint32_t data_size;
    const uint8_t *data;
} lv_img_dsc_t;

typedef struct
{
    lv_coord_t x1;
    lv_coord_t y1;
    lv_coord_t x2;
    lv_coord_t y2;
} lv_area_t;

typedef struct
{
    lv_img_dsc_t Image;
} lv_obj_t;

#define LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(width, height) \
    (3U * (uint32_t)(width) * (uint32_t)(height))

#define LV_OBJ_FLAG_HIDDEN  (1U)

static inline uint32_t lv_img_buf_get_img_size(
    lv_coord_t width,
    lv_coord_t height,
    lv_img_cf_t color_format)
{
    return ((uint32_t)lv_img_cf_get_px_size(color_format) / 8U) *
           (uint32_t)width *
           (uint32_t)height;
}

lv_obj_t *lv_layer_top(void);
lv_obj_t *lv_canvas_create(lv_obj_t *parent);
void lv_obj_add_flag(lv_obj_t *object, uint32_t flag);
void lv_canvas_set_buffer(
    lv_obj_t *object,
    void *buffer,
    lv_coord_t width,
    lv_coord_t height,
    lv_img_cf_t color_format);
void lv_canvas_copy_buf(
    lv_obj_t *object,
    const void *buffer,
    lv_coord_t x,
    lv_coord_t y,
    lv_coord_t width,
    lv_coord_t height);
void lv_canvas_blur_hor(lv_obj_t *object, const lv_area_t *area, uint16_t radius);
void lv_canvas_blur_ver(lv_obj_t *object, const lv_area_t *area, uint16_t radius);
lv_img_dsc_t *lv_canvas_get_img(lv_obj_t *object);
void lv_img_cache_invalidate_src(const void *source);

#endif /* LVGL_H */
