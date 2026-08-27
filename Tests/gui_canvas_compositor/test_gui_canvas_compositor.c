#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Service/gui/canvas/gui_service_canvas_compositor.h"

#undef assert
#define assert(expression)                                                      \
    do                                                                          \
    {                                                                           \
        if (!(expression))                                                      \
        {                                                                       \
            (void)fprintf(                                                      \
                stderr,                                                         \
                "Assertion failed: %s, file %s, line %d\\n",                 \
                #expression,                                                    \
                __FILE__,                                                       \
                __LINE__);                                                      \
            exit(EXIT_FAILURE);                                                 \
        }                                                                       \
    } while (false)

#define TEST_IMAGE_WIDTH       (5U)
#define TEST_IMAGE_HEIGHT      (4U)
#define TEST_PIXEL_SIZE_BYTES  (3U)
#define TEST_IMAGE_SIZE_BYTES  \
    (TEST_IMAGE_WIDTH * TEST_IMAGE_HEIGHT * TEST_PIXEL_SIZE_BYTES)

static uint8_t test_clear_pixels[TEST_IMAGE_SIZE_BYTES];
static uint8_t test_blurred_pixels[TEST_IMAGE_SIZE_BYTES];
static uint8_t test_composite_pixels[TEST_IMAGE_SIZE_BYTES];

static const lv_img_dsc_t test_clear_image = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
        .w = TEST_IMAGE_WIDTH,
        .h = TEST_IMAGE_HEIGHT,
    },
    .data_size = TEST_IMAGE_SIZE_BYTES,
    .data = test_clear_pixels,
};

static const lv_img_dsc_t test_blurred_image = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
        .w = TEST_IMAGE_WIDTH,
        .h = TEST_IMAGE_HEIGHT,
    },
    .data_size = TEST_IMAGE_SIZE_BYTES,
    .data = test_blurred_pixels,
};

static void test_fill_source_images(void)
{
    uint32_t pixel_index;

    for (pixel_index = 0U;
         pixel_index < (TEST_IMAGE_WIDTH * TEST_IMAGE_HEIGHT);
         pixel_index++)
    {
        const uint32_t byte_index = pixel_index * TEST_PIXEL_SIZE_BYTES;

        test_clear_pixels[byte_index] = (uint8_t)(0x10U + pixel_index);
        test_clear_pixels[byte_index + 1U] = (uint8_t)(0x20U + pixel_index);
        test_clear_pixels[byte_index + 2U] = 0xFFU;

        test_blurred_pixels[byte_index] = (uint8_t)(0xA0U + pixel_index);
        test_blurred_pixels[byte_index + 1U] = (uint8_t)(0xB0U + pixel_index);
        test_blurred_pixels[byte_index + 2U] = 0xCCU;
    }
}

static const uint8_t *test_get_pixel(
    const uint8_t *pixels,
    uint32_t x,
    uint32_t y)
{
    return &pixels[
        ((y * TEST_IMAGE_WIDTH) + x) * TEST_PIXEL_SIZE_BYTES];
}

static void test_rectangular_region_replaces_only_its_pixels(void)
{
    const Service_GUI_CanvasBlurRegionTypeDef region = {
        .Area = {
            .x1 = 1,
            .y1 = 1,
            .x2 = 3,
            .y2 = 2,
        },
        .Radius = 0U,
        .Shape = SERVICE_GUI_CANVAS_REGION_ROUNDED_RECT,
    };
    lv_img_dsc_t composite_image;
    uint32_t x;
    uint32_t y;

    test_fill_source_images();
    memset(test_composite_pixels, 0, sizeof(test_composite_pixels));

    assert(service_gui_canvas_compose_blurred_regions(
               &test_clear_image,
               &test_blurred_image,
               &region,
               1U,
               test_composite_pixels,
               sizeof(test_composite_pixels),
               &composite_image) == SERVICE_OK);

    assert(composite_image.data == test_composite_pixels);
    assert(composite_image.data_size == TEST_IMAGE_SIZE_BYTES);

    for (y = 0U; y < TEST_IMAGE_HEIGHT; y++)
    {
        for (x = 0U; x < TEST_IMAGE_WIDTH; x++)
        {
            const bool in_region =
                (x >= 1U) && (x <= 3U) && (y >= 1U) && (y <= 2U);
            const uint8_t *expected = in_region
                                          ? test_get_pixel(test_blurred_pixels, x, y)
                                          : test_get_pixel(test_clear_pixels, x, y);

            assert(memcmp(
                       test_get_pixel(test_composite_pixels, x, y),
                       expected,
                       TEST_PIXEL_SIZE_BYTES) == 0);
        }
    }
}

static void test_circular_region_preserves_its_corner_pixels(void)
{
    const Service_GUI_CanvasBlurRegionTypeDef region = {
        .Area = {
            .x1 = 0,
            .y1 = 0,
            .x2 = 4,
            .y2 = 3,
        },
        .Radius = 0U,
        .Shape = SERVICE_GUI_CANVAS_REGION_CIRCLE,
    };
    lv_img_dsc_t composite_image;
    uint32_t x;
    uint32_t y;

    test_fill_source_images();
    memset(test_composite_pixels, 0, sizeof(test_composite_pixels));

    assert(service_gui_canvas_compose_blurred_regions(
               &test_clear_image,
               &test_blurred_image,
               &region,
               1U,
               test_composite_pixels,
               sizeof(test_composite_pixels),
               &composite_image) == SERVICE_OK);

    for (y = 0U; y < TEST_IMAGE_HEIGHT; y++)
    {
        for (x = 0U; x < TEST_IMAGE_WIDTH; x++)
        {
            const int32_t horizontal_delta_twice = (int32_t)(2U * x) - 4;
            const int32_t vertical_delta_twice = (int32_t)(2U * y) - 3;
            const bool in_circle =
                ((horizontal_delta_twice * horizontal_delta_twice) +
                 (vertical_delta_twice * vertical_delta_twice)) <= 16;
            const uint8_t *expected = in_circle
                                          ? test_get_pixel(test_blurred_pixels, x, y)
                                          : test_get_pixel(test_clear_pixels, x, y);

            assert(memcmp(
                       test_get_pixel(test_composite_pixels, x, y),
                       expected,
                       TEST_PIXEL_SIZE_BYTES) == 0);
        }
    }
}

static void test_rounded_rectangle_preserves_its_corner_pixels(void)
{
    const Service_GUI_CanvasBlurRegionTypeDef region = {
        .Area = {
            .x1 = 0,
            .y1 = 0,
            .x2 = 4,
            .y2 = 3,
        },
        .Radius = 1U,
        .Shape = SERVICE_GUI_CANVAS_REGION_ROUNDED_RECT,
    };
    lv_img_dsc_t composite_image;

    test_fill_source_images();
    memset(test_composite_pixels, 0, sizeof(test_composite_pixels));

    assert(service_gui_canvas_compose_blurred_regions(
               &test_clear_image,
               &test_blurred_image,
               &region,
               1U,
               test_composite_pixels,
               sizeof(test_composite_pixels),
               &composite_image) == SERVICE_OK);

    assert(memcmp(test_get_pixel(test_composite_pixels, 0U, 0U),
                  test_get_pixel(test_clear_pixels, 0U, 0U),
                  TEST_PIXEL_SIZE_BYTES) == 0);
    assert(memcmp(test_get_pixel(test_composite_pixels, 4U, 0U),
                  test_get_pixel(test_clear_pixels, 4U, 0U),
                  TEST_PIXEL_SIZE_BYTES) == 0);
    assert(memcmp(test_get_pixel(test_composite_pixels, 0U, 3U),
                  test_get_pixel(test_clear_pixels, 0U, 3U),
                  TEST_PIXEL_SIZE_BYTES) == 0);
    assert(memcmp(test_get_pixel(test_composite_pixels, 4U, 3U),
                  test_get_pixel(test_clear_pixels, 4U, 3U),
                  TEST_PIXEL_SIZE_BYTES) == 0);

    assert(memcmp(test_get_pixel(test_composite_pixels, 1U, 0U),
                  test_get_pixel(test_blurred_pixels, 1U, 0U),
                  TEST_PIXEL_SIZE_BYTES) == 0);
    assert(memcmp(test_get_pixel(test_composite_pixels, 0U, 1U),
                  test_get_pixel(test_blurred_pixels, 0U, 1U),
                  TEST_PIXEL_SIZE_BYTES) == 0);
    assert(memcmp(test_get_pixel(test_composite_pixels, 2U, 2U),
                  test_get_pixel(test_blurred_pixels, 2U, 2U),
                  TEST_PIXEL_SIZE_BYTES) == 0);
}

static void test_extract_region_returns_contiguous_object_sized_image(void)
{
    const lv_area_t source_area = {
        .x1 = 1,
        .y1 = 1,
        .x2 = 3,
        .y2 = 2,
    };
    uint8_t cropped_pixels[3U * 2U * TEST_PIXEL_SIZE_BYTES];
    lv_img_dsc_t cropped_image;
    uint32_t x;
    uint32_t y;

    test_fill_source_images();
    memset(cropped_pixels, 0, sizeof(cropped_pixels));

    assert(service_gui_canvas_extract_image_region(
               &test_blurred_image,
               &source_area,
               cropped_pixels,
               sizeof(cropped_pixels),
               &cropped_image) == SERVICE_OK);

    assert(cropped_image.header.w == 3U);
    assert(cropped_image.header.h == 2U);
    assert(cropped_image.header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA);
    assert(cropped_image.data_size == sizeof(cropped_pixels));
    assert(cropped_image.data == cropped_pixels);

    for (y = 0U; y < cropped_image.header.h; y++)
    {
        for (x = 0U; x < cropped_image.header.w; x++)
        {
            const uint8_t *const expected = test_get_pixel(
                test_blurred_pixels,
                x + (uint32_t)source_area.x1,
                y + (uint32_t)source_area.y1);
            const uint8_t *const actual = &cropped_pixels[
                ((y * (uint32_t)cropped_image.header.w) + x) *
                TEST_PIXEL_SIZE_BYTES];

            assert(memcmp(actual, expected, TEST_PIXEL_SIZE_BYTES) == 0);
        }
    }
}

static void test_padded_region_keeps_global_coordinates_when_partly_outside(void)
{
    const lv_area_t source_area = {
        .x1 = -1,
        .y1 = 1,
        .x2 = 1,
        .y2 = 2,
    };
    uint8_t cropped_pixels[3U * 2U * TEST_PIXEL_SIZE_BYTES];
    lv_img_dsc_t cropped_image;
    uint32_t y;

    test_fill_source_images();
    memset(cropped_pixels, 0x5AU, sizeof(cropped_pixels));

    assert(service_gui_canvas_extract_image_region_padded(
               &test_blurred_image,
               &source_area,
               cropped_pixels,
               sizeof(cropped_pixels),
               &cropped_image) == SERVICE_OK);

    assert(cropped_image.header.w == 3U);
    assert(cropped_image.header.h == 2U);
    assert(cropped_image.data_size == sizeof(cropped_pixels));

    for (y = 0U; y < cropped_image.header.h; y++)
    {
        const uint8_t *const transparent_pixel = &cropped_pixels[
            (y * (uint32_t)cropped_image.header.w) * TEST_PIXEL_SIZE_BYTES];
        const uint8_t *const source_x0_pixel = &cropped_pixels[
            ((y * (uint32_t)cropped_image.header.w) + 1U) *
            TEST_PIXEL_SIZE_BYTES];
        const uint8_t *const source_x1_pixel = &cropped_pixels[
            ((y * (uint32_t)cropped_image.header.w) + 2U) *
            TEST_PIXEL_SIZE_BYTES];

        assert(transparent_pixel[0] == 0U);
        assert(transparent_pixel[1] == 0U);
        assert(transparent_pixel[2] == 0U);
        assert(memcmp(
                   source_x0_pixel,
                   test_get_pixel(test_blurred_pixels, 0U, y + 1U),
                   TEST_PIXEL_SIZE_BYTES) == 0);
        assert(memcmp(
                   source_x1_pixel,
                   test_get_pixel(test_blurred_pixels, 1U, y + 1U),
                   TEST_PIXEL_SIZE_BYTES) == 0);
    }
}

static void test_out_of_bounds_region_is_rejected(void)
{
    const Service_GUI_CanvasBlurRegionTypeDef region = {
        .Area = {
            .x1 = 0,
            .y1 = 0,
            .x2 = TEST_IMAGE_WIDTH,
            .y2 = 1,
        },
        .Radius = 0U,
        .Shape = SERVICE_GUI_CANVAS_REGION_ROUNDED_RECT,
    };
    lv_img_dsc_t composite_image;

    test_fill_source_images();

    assert(service_gui_canvas_compose_blurred_regions(
               &test_clear_image,
               &test_blurred_image,
               &region,
               1U,
               test_composite_pixels,
               sizeof(test_composite_pixels),
               &composite_image) == SERVICE_INVALID_PARAM);
}

int main(void)
{
    test_rectangular_region_replaces_only_its_pixels();
    test_circular_region_preserves_its_corner_pixels();
    test_rounded_rectangle_preserves_its_corner_pixels();
    test_extract_region_returns_contiguous_object_sized_image();
    test_padded_region_keeps_global_coordinates_when_partly_outside();
    test_out_of_bounds_region_is_rejected();

    puts("GUI Canvas compositor tests passed.");
    return 0;
}
