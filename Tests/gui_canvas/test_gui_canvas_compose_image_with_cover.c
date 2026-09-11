/**
 ******************************************************************************
 * @file    test_gui_canvas_compose_image_with_cover.c
 * @brief   Canvas：复制唱盘底图再叠中心假封面圆。
 ******************************************************************************
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Service/gui/canvas/gui_service_canvas_compositor.h"
#include "Service/service.h"

#define TEST_PIXEL_BYTES  3U
#define TEST_DISC_RGB     0x334455U
#define TEST_COVER_RGB    0x00B0DEU

/**
 * @brief 把 0xRRGGBB 收成 RGB565，与 compositor 打包规则一致。
 * @param[in] rgb 24-bit RGB。
 * @return RGB565。
 */
static uint16_t test_rgb888_to_rgb565(uint32_t rgb)
{
    uint32_t red = (rgb >> 16) & 0xFFU;
    uint32_t green = (rgb >> 8) & 0xFFU;
    uint32_t blue = rgb & 0xFFU;

    return (uint16_t)(((red & 0xF8U) << 8) | ((green & 0xFCU) << 3) | (blue >> 3));
}

/**
 * @brief 写入一个 TRUE_COLOR_ALPHA 像素。
 * @param[in,out] buffer 连续像素缓冲。
 * @param[in] width 图片宽度。
 * @param[in] x 列。
 * @param[in] y 行。
 * @param[in] rgb565 RGB565。
 * @param[in] alpha Alpha。
 */
static void test_write_pixel(
    uint8_t *buffer,
    uint16_t width,
    uint16_t x,
    uint16_t y,
    uint16_t rgb565,
    uint8_t alpha)
{
    uint32_t offset =
        (((uint32_t)y * (uint32_t)width) + (uint32_t)x) * TEST_PIXEL_BYTES;

    buffer[offset] = (uint8_t)(rgb565 & 0xFFU);
    buffer[offset + 1U] = (uint8_t)((rgb565 >> 8) & 0xFFU);
    buffer[offset + 2U] = alpha;
}

/**
 * @brief 读取 TRUE_COLOR_ALPHA 像素的 RGB565 与 Alpha。
 * @param[in] buffer 连续像素缓冲。
 * @param[in] width 图片宽度。
 * @param[in] x 列。
 * @param[in] y 行。
 * @param[out] rgb565 RGB565。
 * @param[out] alpha Alpha。
 */
static void test_read_pixel(
    const uint8_t *buffer,
    uint16_t width,
    uint16_t x,
    uint16_t y,
    uint16_t *rgb565,
    uint8_t *alpha)
{
    uint32_t offset =
        (((uint32_t)y * (uint32_t)width) + (uint32_t)x) * TEST_PIXEL_BYTES;

    *rgb565 = (uint16_t)buffer[offset] |
              (uint16_t)((uint16_t)buffer[offset + 1U] << 8);
    *alpha = buffer[offset + 2U];
}

/**
 * @brief 构造 8×8 底图：四角透明，其余为唱盘色。
 * @param[out] buffer 像素缓冲。
 * @param[out] image 绑定该缓冲的描述符。
 */
static void test_fill_square_base(uint8_t *buffer, lv_img_dsc_t *image)
{
    uint16_t disc_rgb565 = test_rgb888_to_rgb565(TEST_DISC_RGB);
    uint16_t y;

    memset(buffer, 0xA5U, 8U * 8U * TEST_PIXEL_BYTES);

    for (y = 0U; y < 8U; y++)
    {
        uint16_t x;

        for (x = 0U; x < 8U; x++)
        {
            uint8_t alpha =
                ((x == 0U) || (x == 7U) || (y == 0U) || (y == 7U)) ? 0U : 255U;

            test_write_pixel(buffer, 8U, x, y, disc_rgb565, alpha);
        }
    }

    memset(image, 0, sizeof(*image));
    image->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    image->header.w = 8U;
    image->header.h = 8U;
    image->data_size = 8U * 8U * TEST_PIXEL_BYTES;
    image->data = buffer;
}

/**
 * @brief 底图四角透明被保留，中心 4px 封面覆盖唱盘色，且不改源缓冲。
 */
static void test_copies_base_then_paints_center_cover(void)
{
    uint8_t source[8U * 8U * TEST_PIXEL_BYTES];
    uint8_t source_copy[8U * 8U * TEST_PIXEL_BYTES];
    uint8_t dest[8U * 8U * TEST_PIXEL_BYTES];
    lv_img_dsc_t base;
    lv_img_dsc_t image;
    uint16_t rgb565;
    uint8_t alpha;

    test_fill_square_base(source, &base);
    memcpy(source_copy, source, sizeof(source));
    memset(dest, 0x5AU, sizeof(dest));

    assert(service_gui_canvas_compose_image_with_cover_circle(
               &base,
               4U,
               TEST_COVER_RGB,
               dest,
               (uint32_t)sizeof(dest),
               &image) == SERVICE_OK);

    assert(image.header.w == 8U);
    assert(image.header.h == 8U);
    assert(image.header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA);
    assert(image.data == dest);
    assert(image.data_size == sizeof(dest));
    assert(memcmp(source, source_copy, sizeof(source)) == 0);

    test_read_pixel(dest, 8U, 0U, 0U, &rgb565, &alpha);
    assert(alpha == 0U);
    assert(rgb565 == test_rgb888_to_rgb565(TEST_DISC_RGB));

    test_read_pixel(dest, 8U, 4U, 1U, &rgb565, &alpha);
    assert(alpha == 255U);
    assert(rgb565 == test_rgb888_to_rgb565(TEST_DISC_RGB));

    test_read_pixel(dest, 8U, 4U, 4U, &rgb565, &alpha);
    assert(alpha == 255U);
    assert(rgb565 == test_rgb888_to_rgb565(TEST_COVER_RGB));
}

/**
 * @brief 封面直径为 0 时整图等于底图。
 */
static void test_zero_cover_keeps_base_pixels(void)
{
    uint8_t source[8U * 8U * TEST_PIXEL_BYTES];
    uint8_t dest[8U * 8U * TEST_PIXEL_BYTES];
    lv_img_dsc_t base;
    lv_img_dsc_t image;
    uint16_t rgb565;
    uint8_t alpha;

    test_fill_square_base(source, &base);

    assert(service_gui_canvas_compose_image_with_cover_circle(
               &base,
               0U,
               TEST_COVER_RGB,
               dest,
               (uint32_t)sizeof(dest),
               &image) == SERVICE_OK);

    test_read_pixel(dest, 8U, 4U, 4U, &rgb565, &alpha);
    assert(alpha == 255U);
    assert(rgb565 == test_rgb888_to_rgb565(TEST_DISC_RGB));
}

/**
 * @brief 非法尺寸、空指针、缓冲重叠失败且不写描述符尺寸。
 */
static void test_invalid_params_fail(void)
{
    uint8_t source[8U * 8U * TEST_PIXEL_BYTES];
    uint8_t dest[8U * 8U * TEST_PIXEL_BYTES];
    lv_img_dsc_t base;
    lv_img_dsc_t image;

    test_fill_square_base(source, &base);
    image.header.w = 1U;
    image.data_size = 1U;

    assert(service_gui_canvas_compose_image_with_cover_circle(
               NULL,
               4U,
               TEST_COVER_RGB,
               dest,
               (uint32_t)sizeof(dest),
               &image) == SERVICE_INVALID_PARAM);
    assert(service_gui_canvas_compose_image_with_cover_circle(
               &base,
               9U,
               TEST_COVER_RGB,
               dest,
               (uint32_t)sizeof(dest),
               &image) == SERVICE_INVALID_PARAM);
    assert(service_gui_canvas_compose_image_with_cover_circle(
               &base,
               4U,
               TEST_COVER_RGB,
               NULL,
               (uint32_t)sizeof(dest),
               &image) == SERVICE_INVALID_PARAM);
    assert(service_gui_canvas_compose_image_with_cover_circle(
               &base,
               4U,
               TEST_COVER_RGB,
               dest,
               (uint32_t)sizeof(dest) - 1U,
               &image) == SERVICE_INVALID_PARAM);
    assert(service_gui_canvas_compose_image_with_cover_circle(
               &base,
               4U,
               TEST_COVER_RGB,
               source,
               (uint32_t)sizeof(source),
               &image) == SERVICE_INVALID_PARAM);

    base.header.h = 7U;
    assert(service_gui_canvas_compose_image_with_cover_circle(
               &base,
               4U,
               TEST_COVER_RGB,
               dest,
               (uint32_t)sizeof(dest),
               &image) == SERVICE_INVALID_PARAM);

    assert(image.header.w == 1U);
    assert(image.data_size == 1U);
}

/**
 * @brief 运行全部底图叠封面测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_copies_base_then_paints_center_cover();
    test_zero_cover_keeps_base_pixels();
    test_invalid_params_fail();

    puts("gui_canvas_compose_image_with_cover_tests: all tests passed");
    return 0;
}
