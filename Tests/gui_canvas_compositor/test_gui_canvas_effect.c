#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Platform/lcd/platform_lcd.h"
#include "Service/gui/canvas/gui_service_canvas.h"

#define TEST_WALLPAPER_SIZE_BYTES \
    (PLATFORM_LCD_WIDTH * PLATFORM_LCD_HEIGHT * 3U)

static uint8_t test_source_pixels[TEST_WALLPAPER_SIZE_BYTES];
static lv_obj_t test_top_layer;
static lv_obj_t test_canvas;

lv_obj_t *lv_layer_top(void)
{
    return &test_top_layer;
}

lv_obj_t *lv_canvas_create(lv_obj_t *parent)
{
    (void)parent;
    memset(&test_canvas, 0, sizeof(test_canvas));
    return &test_canvas;
}

void lv_obj_add_flag(lv_obj_t *object, uint32_t flag)
{
    (void)object;
    (void)flag;
}

void lv_canvas_set_buffer(
    lv_obj_t *object,
    void *buffer,
    lv_coord_t width,
    lv_coord_t height,
    lv_img_cf_t color_format)
{
    object->Image.header.w = width;
    object->Image.header.h = height;
    object->Image.header.cf = color_format;
    object->Image.data = buffer;

    /* 模拟 LVGL v8：该 API 不填写 Canvas 描述符的 data_size。 */
    object->Image.data_size = 0U;
}

void lv_canvas_copy_buf(
    lv_obj_t *object,
    const void *buffer,
    lv_coord_t x,
    lv_coord_t y,
    lv_coord_t width,
    lv_coord_t height)
{
    (void)x;
    (void)y;
    memcpy(
        (void *)object->Image.data,
        buffer,
        lv_img_buf_get_img_size(width, height, object->Image.header.cf));
}

void lv_canvas_blur_hor(lv_obj_t *object, const lv_area_t *area, uint16_t radius)
{
    (void)object;
    (void)area;
    (void)radius;
}

void lv_canvas_blur_ver(lv_obj_t *object, const lv_area_t *area, uint16_t radius)
{
    (void)object;
    (void)area;
    (void)radius;
}

lv_img_dsc_t *lv_canvas_get_img(lv_obj_t *object)
{
    return &object->Image;
}

void lv_img_cache_invalidate_src(const void *source)
{
    (void)source;
}

static void test_blur_output_has_complete_image_metadata(void)
{
    const lv_img_dsc_t source = {
        .header = {
            .cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
            .w = PLATFORM_LCD_WIDTH,
            .h = PLATFORM_LCD_HEIGHT,
        },
        .data_size = sizeof(test_source_pixels),
        .data = test_source_pixels,
    };
    lv_img_dsc_t *blurred_image = NULL;

    assert(service_gui_canvas_blur_image(
               &source,
               10U,
               &blurred_image) == SERVICE_OK);
    assert(blurred_image != NULL);
    assert(blurred_image != lv_canvas_get_img(&test_canvas));
    assert(blurred_image->header.w == PLATFORM_LCD_WIDTH);
    assert(blurred_image->header.h == PLATFORM_LCD_HEIGHT);
    assert(blurred_image->header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA);
    assert(blurred_image->data_size == sizeof(test_source_pixels));
    assert(blurred_image->data != NULL);
}

int main(void)
{
    test_blur_output_has_complete_image_metadata();

    puts("GUI Canvas effect tests passed.");
    return 0;
}
