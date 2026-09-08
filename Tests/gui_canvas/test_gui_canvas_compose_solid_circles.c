/**
 ******************************************************************************
 * @file    test_gui_canvas_compose_solid_circles.c
 * @brief   Canvas 纯色圆合成：唱盘圆 + 中心假封面圆。
 ******************************************************************************
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Service/gui/canvas/gui_service_canvas_compositor.h"
#include "Service/service.h"

#define TEST_PIXEL_BYTES  3U
#define TEST_DISC_RGB     0x202020U
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
 * @brief 8px 唱盘圆角外透明，圆心为唱盘色，中心 4px 为封面色。
 */
static void test_disc_and_cover_use_different_solid_colors(void)
{
    uint8_t buffer[8U * 8U * TEST_PIXEL_BYTES];
    lv_img_dsc_t image;
    uint16_t rgb565;
    uint8_t alpha;
    Service_StatusTypeDef status;

    memset(buffer, 0xA5U, sizeof(buffer));

    status = service_gui_canvas_compose_solid_circles(
        8U,
        TEST_DISC_RGB,
        4U,
        TEST_COVER_RGB,
        buffer,
        (uint32_t)sizeof(buffer),
        &image);

    assert(status == SERVICE_OK);
    assert(image.header.w == 8U);
    assert(image.header.h == 8U);
    assert(image.header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA);
    assert(image.data == buffer);
    assert(image.data_size == sizeof(buffer));

    test_read_pixel(buffer, 8U, 0U, 0U, &rgb565, &alpha);
    assert(alpha == 0U);

    test_read_pixel(buffer, 8U, 4U, 1U, &rgb565, &alpha);
    assert(alpha == 255U);
    assert(rgb565 == test_rgb888_to_rgb565(TEST_DISC_RGB));

    test_read_pixel(buffer, 8U, 4U, 4U, &rgb565, &alpha);
    assert(alpha == 255U);
    assert(rgb565 == test_rgb888_to_rgb565(TEST_COVER_RGB));
}

/**
 * @brief 封面直径为 0 时整圆都是唱盘色。
 */
static void test_zero_cover_keeps_disc_color_at_center(void)
{
    uint8_t buffer[8U * 8U * TEST_PIXEL_BYTES];
    lv_img_dsc_t image;
    uint16_t rgb565;
    uint8_t alpha;

    assert(service_gui_canvas_compose_solid_circles(
               8U,
               TEST_DISC_RGB,
               0U,
               TEST_COVER_RGB,
               buffer,
               (uint32_t)sizeof(buffer),
               &image) == SERVICE_OK);

    test_read_pixel(buffer, 8U, 4U, 4U, &rgb565, &alpha);
    assert(alpha == 255U);
    assert(rgb565 == test_rgb888_to_rgb565(TEST_DISC_RGB));
}

/**
 * @brief 非法尺寸或空指针失败且不写描述符尺寸。
 */
static void test_invalid_params_fail(void)
{
    uint8_t buffer[8U * 8U * TEST_PIXEL_BYTES];
    lv_img_dsc_t image;

    image.header.w = 1U;
    image.data_size = 1U;

    assert(service_gui_canvas_compose_solid_circles(
               0U,
               TEST_DISC_RGB,
               0U,
               TEST_COVER_RGB,
               buffer,
               (uint32_t)sizeof(buffer),
               &image) == SERVICE_INVALID_PARAM);
    assert(service_gui_canvas_compose_solid_circles(
               8U,
               TEST_DISC_RGB,
               9U,
               TEST_COVER_RGB,
               buffer,
               (uint32_t)sizeof(buffer),
               &image) == SERVICE_INVALID_PARAM);
    assert(service_gui_canvas_compose_solid_circles(
               8U,
               TEST_DISC_RGB,
               4U,
               TEST_COVER_RGB,
               NULL,
               (uint32_t)sizeof(buffer),
               &image) == SERVICE_INVALID_PARAM);
    assert(service_gui_canvas_compose_solid_circles(
               8U,
               TEST_DISC_RGB,
               4U,
               TEST_COVER_RGB,
               buffer,
               (uint32_t)sizeof(buffer) - 1U,
               &image) == SERVICE_INVALID_PARAM);
    assert(image.header.w == 1U);
    assert(image.data_size == 1U);
}

/**
 * @brief 运行全部纯色圆合成测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_disc_and_cover_use_different_solid_colors();
    test_zero_cover_keeps_disc_color_at_center();
    test_invalid_params_fail();

    puts("gui_canvas_compose_solid_circles_tests: all tests passed");
    return 0;
}
