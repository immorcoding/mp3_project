/**
  ******************************************************************************
  * @file    gui_service_canvas_compositor.c
  * @brief   GUI Service 私有局部毛玻璃合成实现。
  ******************************************************************************
  */

#include "Service/gui/canvas/gui_service_canvas_compositor.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/**
 * @brief 判断当前 Canvas 合成与裁剪路径支持的图片描述符是否有效。
 * @param image 待校验的真彩带 Alpha 图片。
 * @return 图片可安全按像素复制时返回 true。
 */
static bool service_gui_canvas_is_supported_image(const lv_img_dsc_t *image)
{
    if (image == NULL)
    {
        return false;
    }

    const uint32_t pixel_size_bytes =
        (uint32_t)lv_img_cf_get_px_size(image->header.cf) / 8U;
    const uint32_t required_size =
        (uint32_t)image->header.w *
        (uint32_t)image->header.h *
        pixel_size_bytes;

    return (image->data != NULL) &&
           (image->header.w > 0) &&
           (image->header.h > 0) &&
           (image->header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA) &&
           (pixel_size_bytes != 0U) &&
           (image->data_size == required_size);
}

/**
 * @brief 将一个无圆角矩形区域的模糊像素覆盖到合成背景。
 * @param clear_image 清晰壁纸，仅用于读取图像格式与尺寸。
 * @param blurred_image 全屏模糊壁纸。
 * @param region 要覆盖的矩形区域。
 * @param composite_buffer 已预先复制清晰壁纸的输出缓冲。
 */
static void service_gui_canvas_copy_rectangular_region(
    const lv_img_dsc_t *clear_image,
    const lv_img_dsc_t *blurred_image,
    const Service_GUI_CanvasBlurRegionTypeDef *region,
    uint8_t *composite_buffer)
{
    const uint32_t pixel_size_bytes =
        (uint32_t)lv_img_cf_get_px_size(clear_image->header.cf) / 8U;
    const uint32_t row_size_bytes =
        (uint32_t)(region->Area.x2 - region->Area.x1 + 1) *
        pixel_size_bytes;
    uint32_t y;

    for (y = (uint32_t)region->Area.y1;
         y <= (uint32_t)region->Area.y2;
         y++)
    {
        const uint32_t offset =
            ((y * (uint32_t)clear_image->header.w) +
             (uint32_t)region->Area.x1) *
            pixel_size_bytes;

        memcpy(
            &composite_buffer[offset],
            &blurred_image->data[offset],
            row_size_bytes);
    }
}

/**
 * @brief 从完整图片复制一个连续存储的矩形子图。
 * @param source_image 输入完整图片。
 * @param source_area 以输入图片左上角为原点的闭区间裁剪区域。
 * @param cropped_buffer 调用方持有的连续输出像素缓冲。
 * @param cropped_buffer_size 输出缓冲大小，单位为字节。
 * @param cropped_image 返回绑定输出缓冲的子图描述符。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_INVALID_PARAM 图片、区域或输出缓冲不满足约束。
 * @note 输出缓冲不得与输入图片像素缓冲重叠。当前调用方分别持有 Canvas 工作帧
 *       和页面级背景，满足该约束。
 */
Service_StatusTypeDef service_gui_canvas_extract_image_region(
    const lv_img_dsc_t *source_image,
    const lv_area_t *source_area,
    uint8_t *cropped_buffer,
    uint32_t cropped_buffer_size,
    lv_img_dsc_t *cropped_image)
{
    uint32_t pixel_size_bytes;
    uint32_t cropped_width;
    uint32_t cropped_height;
    uint32_t cropped_data_size;
    uint32_t row_index;

    if (!service_gui_canvas_is_supported_image(source_image) ||
        (source_area == NULL) ||
        (cropped_buffer == NULL) ||
        (cropped_image == NULL) ||
        (source_area->x1 < 0) ||
        (source_area->y1 < 0) ||
        (source_area->x2 < source_area->x1) ||
        (source_area->y2 < source_area->y1) ||
        (source_area->x2 >= source_image->header.w) ||
        (source_area->y2 >= source_image->header.h))
    {
        return SERVICE_INVALID_PARAM;
    }

    pixel_size_bytes =
        (uint32_t)lv_img_cf_get_px_size(source_image->header.cf) / 8U;
    cropped_width =
        (uint32_t)source_area->x2 - (uint32_t)source_area->x1 + 1U;
    cropped_height =
        (uint32_t)source_area->y2 - (uint32_t)source_area->y1 + 1U;
    cropped_data_size = cropped_width * cropped_height * pixel_size_bytes;

    if (cropped_buffer_size < cropped_data_size)
    {
        return SERVICE_INVALID_PARAM;
    }

    for (row_index = 0U; row_index < cropped_height; row_index++)
    {
        const uint32_t source_offset =
            ((((uint32_t)source_area->y1 + row_index) *
              (uint32_t)source_image->header.w) +
             (uint32_t)source_area->x1) *
            pixel_size_bytes;
        const uint32_t cropped_offset =
            (row_index * cropped_width) * pixel_size_bytes;

        memcpy(
            &cropped_buffer[cropped_offset],
            &source_image->data[source_offset],
            cropped_width * pixel_size_bytes);
    }

    *cropped_image = *source_image;
    cropped_image->header.w = cropped_width;
    cropped_image->header.h = cropped_height;
    cropped_image->data_size = cropped_data_size;
    cropped_image->data = cropped_buffer;

    return SERVICE_OK;
}

/**
 * @brief 从完整图片复制带透明越界填充的连续矩形子图。
 * @param source_image 输入完整图片。
 * @param source_area 以输入图片左上角为原点的闭区间裁剪区域，可越界。
 * @param cropped_buffer 调用方持有的连续输出像素缓冲。
 * @param cropped_buffer_size 输出缓冲大小，单位为字节。
 * @param cropped_image 返回绑定输出缓冲的子图描述符。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_INVALID_PARAM 图片、区域或输出缓冲不满足约束。
 * @note 先将整个输出区域清零，再仅复制与输入图片相交的像素。因此对象横滑至
 *       屏幕边缘时，其可见部分仍对应全局背景坐标，越出壁纸的部分保持透明。
 */
Service_StatusTypeDef service_gui_canvas_extract_image_region_padded(
    const lv_img_dsc_t *source_image,
    const lv_area_t *source_area,
    uint8_t *cropped_buffer,
    uint32_t cropped_buffer_size,
    lv_img_dsc_t *cropped_image)
{
    uint32_t pixel_size_bytes;
    uint32_t cropped_width;
    uint32_t cropped_height;
    uint32_t cropped_data_size;
    int32_t intersection_x1;
    int32_t intersection_y1;
    int32_t intersection_x2;
    int32_t intersection_y2;
    int32_t y;

    if (!service_gui_canvas_is_supported_image(source_image) ||
        (source_area == NULL) ||
        (cropped_buffer == NULL) ||
        (cropped_image == NULL) ||
        (source_area->x2 < source_area->x1) ||
        (source_area->y2 < source_area->y1))
    {
        return SERVICE_INVALID_PARAM;
    }

    pixel_size_bytes =
        (uint32_t)lv_img_cf_get_px_size(source_image->header.cf) / 8U;
    cropped_width =
        (uint32_t)((int32_t)source_area->x2 - (int32_t)source_area->x1 + 1);
    cropped_height =
        (uint32_t)((int32_t)source_area->y2 - (int32_t)source_area->y1 + 1);

    if ((cropped_width > UINT16_MAX) ||
        (cropped_height > UINT16_MAX) ||
        (cropped_width > (UINT32_MAX / pixel_size_bytes)) ||
        (cropped_height >
         (UINT32_MAX / (cropped_width * pixel_size_bytes))))
    {
        return SERVICE_INVALID_PARAM;
    }

    cropped_data_size = cropped_width * cropped_height * pixel_size_bytes;

    if (cropped_buffer_size < cropped_data_size)
    {
        return SERVICE_INVALID_PARAM;
    }

    memset(cropped_buffer, 0, cropped_data_size);

    intersection_x1 = (source_area->x1 > 0) ? source_area->x1 : 0;
    intersection_y1 = (source_area->y1 > 0) ? source_area->y1 : 0;
    intersection_x2 = ((int32_t)source_area->x2 <
                       ((int32_t)source_image->header.w - 1))
                          ? source_area->x2
                          : ((int32_t)source_image->header.w - 1);
    intersection_y2 = ((int32_t)source_area->y2 <
                       ((int32_t)source_image->header.h - 1))
                          ? source_area->y2
                          : ((int32_t)source_image->header.h - 1);

    if ((intersection_x1 <= intersection_x2) &&
        (intersection_y1 <= intersection_y2))
    {
        const uint32_t copied_width =
            (uint32_t)(intersection_x2 - intersection_x1 + 1);
        const uint32_t copied_row_size = copied_width * pixel_size_bytes;

        for (y = intersection_y1; y <= intersection_y2; y++)
        {
            const uint32_t source_offset =
                ((((uint32_t)y * (uint32_t)source_image->header.w) +
                  (uint32_t)intersection_x1) *
                 pixel_size_bytes);
            const uint32_t cropped_offset =
                ((((uint32_t)(y - (int32_t)source_area->y1) * cropped_width) +
                  (uint32_t)(intersection_x1 - (int32_t)source_area->x1)) *
                 pixel_size_bytes);

            memcpy(
                &cropped_buffer[cropped_offset],
                &source_image->data[source_offset],
                copied_row_size);
        }
    }

    *cropped_image = *source_image;
    cropped_image->header.w = cropped_width;
    cropped_image->header.h = cropped_height;
    cropped_image->data_size = cropped_data_size;
    cropped_image->data = cropped_buffer;

    return SERVICE_OK;
}

/**
 * @brief 判断一个像素是否处于区域短边决定直径的内接圆内。
 * @param region 圆形区域描述。
 * @param x 待判断像素的 X 坐标。
 * @param y 待判断像素的 Y 坐标。
 * @return 位于圆内或圆周上时返回 true。
 * @note 使用二倍坐标避免偶数尺寸区域出现半像素圆心时的浮点运算和偏置。
 */
static bool service_gui_canvas_is_inside_circle(
    const Service_GUI_CanvasBlurRegionTypeDef *region,
    int32_t x,
    int32_t y)
{
    const int32_t width =
        (int32_t)region->Area.x2 - (int32_t)region->Area.x1 + 1;
    const int32_t height =
        (int32_t)region->Area.y2 - (int32_t)region->Area.y1 + 1;
    const int32_t diameter = (width < height) ? width : height;
    const int32_t horizontal_delta_twice =
        (2 * x) - ((int32_t)region->Area.x1 + (int32_t)region->Area.x2);
    const int32_t vertical_delta_twice =
        (2 * y) - ((int32_t)region->Area.y1 + (int32_t)region->Area.y2);

    return ((horizontal_delta_twice * horizontal_delta_twice) +
            (vertical_delta_twice * vertical_delta_twice)) <=
           (diameter * diameter);
}

/**
 * @brief 判断一个像素是否位于圆角矩形内部。
 * @param region 圆角矩形区域描述。
 * @param x 待判断像素的 X 坐标。
 * @param y 待判断像素的 Y 坐标。
 * @return 位于矩形可见区域内时返回 true。
 * @note Radius 大于区域短边的一半时会被钳制，以兼容 SquareLine 的大 Radius
 *       样式值；零 Radius 等价于普通矩形。
 */
static bool service_gui_canvas_is_inside_rounded_rect(
    const Service_GUI_CanvasBlurRegionTypeDef *region,
    int32_t x,
    int32_t y)
{
    const int32_t width =
        (int32_t)region->Area.x2 - (int32_t)region->Area.x1 + 1;
    const int32_t height =
        (int32_t)region->Area.y2 - (int32_t)region->Area.y1 + 1;
    const int32_t maximum_radius =
        ((width < height) ? width : height) / 2;
    const int32_t radius =
        ((int32_t)region->Radius < maximum_radius)
            ? (int32_t)region->Radius
            : maximum_radius;
    int32_t corner_center_x;
    int32_t corner_center_y;
    int32_t horizontal_delta;
    int32_t vertical_delta;

    if (radius == 0)
    {
        return true;
    }

    if (x < ((int32_t)region->Area.x1 + radius))
    {
        corner_center_x = (int32_t)region->Area.x1 + radius;
    }
    else if (x > ((int32_t)region->Area.x2 - radius))
    {
        corner_center_x = (int32_t)region->Area.x2 - radius;
    }
    else
    {
        return true;
    }

    if (y < ((int32_t)region->Area.y1 + radius))
    {
        corner_center_y = (int32_t)region->Area.y1 + radius;
    }
    else if (y > ((int32_t)region->Area.y2 - radius))
    {
        corner_center_y = (int32_t)region->Area.y2 - radius;
    }
    else
    {
        return true;
    }

    horizontal_delta = x - corner_center_x;
    vertical_delta = y - corner_center_y;

    return ((horizontal_delta * horizontal_delta) +
            (vertical_delta * vertical_delta)) <=
           (radius * radius);
}

/**
 * @brief 将一个圆形区域的模糊像素覆盖到合成背景。
 * @param clear_image 清晰壁纸，仅用于读取图像格式与尺寸。
 * @param blurred_image 全屏模糊壁纸。
 * @param region 要覆盖的圆形区域。
 * @param composite_buffer 已预先复制清晰壁纸的输出缓冲。
 */
static void service_gui_canvas_copy_circular_region(
    const lv_img_dsc_t *clear_image,
    const lv_img_dsc_t *blurred_image,
    const Service_GUI_CanvasBlurRegionTypeDef *region,
    uint8_t *composite_buffer)
{
    const uint32_t pixel_size_bytes =
        (uint32_t)lv_img_cf_get_px_size(clear_image->header.cf) / 8U;
    int32_t y;

    for (y = region->Area.y1; y <= region->Area.y2; y++)
    {
        int32_t x;

        for (x = region->Area.x1; x <= region->Area.x2; x++)
        {
            uint32_t offset;

            if (!service_gui_canvas_is_inside_circle(region, x, y))
            {
                continue;
            }

            offset =
                (((uint32_t)y * (uint32_t)clear_image->header.w) +
                 (uint32_t)x) *
                pixel_size_bytes;
            memcpy(
                &composite_buffer[offset],
                &blurred_image->data[offset],
                pixel_size_bytes);
        }
    }
}

/**
 * @brief 将一个圆角矩形区域的模糊像素覆盖到合成背景。
 * @param clear_image 清晰壁纸，仅用于读取图像格式与尺寸。
 * @param blurred_image 全屏模糊壁纸。
 * @param region 要覆盖的圆角矩形区域。
 * @param composite_buffer 已预先复制清晰壁纸的输出缓冲。
 */
static void service_gui_canvas_copy_rounded_rect_region(
    const lv_img_dsc_t *clear_image,
    const lv_img_dsc_t *blurred_image,
    const Service_GUI_CanvasBlurRegionTypeDef *region,
    uint8_t *composite_buffer)
{
    const uint32_t pixel_size_bytes =
        (uint32_t)lv_img_cf_get_px_size(clear_image->header.cf) / 8U;
    int32_t y;

    for (y = region->Area.y1; y <= region->Area.y2; y++)
    {
        int32_t x;

        for (x = region->Area.x1; x <= region->Area.x2; x++)
        {
            uint32_t offset;

            if (!service_gui_canvas_is_inside_rounded_rect(region, x, y))
            {
                continue;
            }

            offset =
                (((uint32_t)y * (uint32_t)clear_image->header.w) +
                 (uint32_t)x) *
                pixel_size_bytes;
            memcpy(
                &composite_buffer[offset],
                &blurred_image->data[offset],
                pixel_size_bytes);
        }
    }
}

/**
 * @brief 将清晰图片与若干局部模糊区域合成为一张长期背景图片。
 * @param clear_image 原始清晰壁纸。
 * @param blurred_image 与清晰壁纸同尺寸、同格式的全屏模糊帧。
 * @param regions 需要替换为模糊像素的区域描述数组。
 * @param region_count 区域数量。
 * @param composite_buffer 调用方长期持有的输出像素缓冲。
 * @param composite_buffer_size 输出缓冲字节数。
 * @param composite_image 返回绑定输出缓冲的图片描述符。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_INVALID_PARAM 图片、区域或输出缓冲不满足当前合成约束。
 * @note 当前实现支持圆角矩形和以区域短边为直径的圆形。输出图片与输入图片
 *       保持相同的格式和尺寸。
 */
Service_StatusTypeDef service_gui_canvas_compose_blurred_regions(
    const lv_img_dsc_t *clear_image,
    const lv_img_dsc_t *blurred_image,
    const Service_GUI_CanvasBlurRegionTypeDef *regions,
    uint32_t region_count,
    uint8_t *composite_buffer,
    uint32_t composite_buffer_size,
    lv_img_dsc_t *composite_image)
{
    uint32_t region_index;

    if ((clear_image == NULL) ||
        (blurred_image == NULL) ||
        (composite_buffer == NULL) ||
        (composite_image == NULL) ||
        !service_gui_canvas_is_supported_image(clear_image) ||
        !service_gui_canvas_is_supported_image(blurred_image) ||
        (clear_image->header.w != blurred_image->header.w) ||
        (clear_image->header.h != blurred_image->header.h) ||
        (clear_image->header.cf != blurred_image->header.cf) ||
        (clear_image->data_size != blurred_image->data_size) ||
        (composite_buffer_size < clear_image->data_size) ||
        ((region_count != 0U) && (regions == NULL)))
    {
        return SERVICE_INVALID_PARAM;
    }

    memcpy(composite_buffer, clear_image->data, clear_image->data_size);

    for (region_index = 0U; region_index < region_count; region_index++)
    {
        const Service_GUI_CanvasBlurRegionTypeDef *const region =
            &regions[region_index];

        if (((region->Shape != SERVICE_GUI_CANVAS_REGION_ROUNDED_RECT) &&
             (region->Shape != SERVICE_GUI_CANVAS_REGION_CIRCLE)) ||
            (region->Area.x1 < 0) ||
            (region->Area.y1 < 0) ||
            (region->Area.x2 < region->Area.x1) ||
            (region->Area.y2 < region->Area.y1) ||
            (region->Area.x2 >= clear_image->header.w) ||
            (region->Area.y2 >= clear_image->header.h))
        {
            return SERVICE_INVALID_PARAM;
        }

        if (region->Shape == SERVICE_GUI_CANVAS_REGION_CIRCLE)
        {
            service_gui_canvas_copy_circular_region(
                clear_image,
                blurred_image,
                region,
                composite_buffer);
        }
        else
        {
            if (region->Radius == 0U)
            {
                service_gui_canvas_copy_rectangular_region(
                    clear_image,
                    blurred_image,
                    region,
                    composite_buffer);
            }
            else
            {
                service_gui_canvas_copy_rounded_rect_region(
                    clear_image,
                    blurred_image,
                    region,
                    composite_buffer);
            }
        }
    }

    *composite_image = *clear_image;
    composite_image->data = composite_buffer;

    return SERVICE_OK;
}

/**
 * @brief 把 0xRRGGBB 收成 RGB565。
 * @param[in] rgb 24-bit RGB。
 * @return RGB565。
 */
static uint16_t service_gui_canvas_rgb888_to_rgb565(uint32_t rgb)
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
static void service_gui_canvas_write_true_color_alpha_pixel(
    uint8_t *buffer,
    uint32_t width,
    int32_t x,
    int32_t y,
    uint16_t rgb565,
    uint8_t alpha)
{
    uint32_t offset =
        (((uint32_t)y * width) + (uint32_t)x) * 3U;

    buffer[offset] = (uint8_t)(rgb565 & 0xFFU);
    buffer[offset + 1U] = (uint8_t)((rgb565 >> 8) & 0xFFU);
    buffer[offset + 2U] = alpha;
}

/**
 * @brief 在正方形缓冲里合成唱盘纯色圆，并可叠中心假封面圆。
 * @param[in] diameter 输出正方形边长，也是唱盘直径，单位为像素。
 * @param[in] disc_rgb 唱盘 0xRRGGBB。
 * @param[in] cover_diameter 中心封面直径；0 表示不叠封面。
 * @param[in] cover_rgb 封面 0xRRGGBB。
 * @param[out] buffer 调用方持有的输出像素缓冲。
 * @param[in] buffer_size 输出缓冲字节数。
 * @param[out] image 绑定输出缓冲的 TRUE_COLOR_ALPHA 描述符。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_INVALID_PARAM 尺寸、封面直径或缓冲不满足约束。
 * @note 圆外像素 Alpha 为 0。失败时不改 image。
 */
Service_StatusTypeDef service_gui_canvas_compose_solid_circles(
    uint16_t diameter,
    uint32_t disc_rgb,
    uint16_t cover_diameter,
    uint32_t cover_rgb,
    uint8_t *buffer,
    uint32_t buffer_size,
    lv_img_dsc_t *image)
{
    uint32_t required_bytes;
    uint16_t disc_rgb565;
    uint16_t cover_rgb565;
    Service_GUI_CanvasBlurRegionTypeDef disc_region;
    Service_GUI_CanvasBlurRegionTypeDef cover_region;
    int32_t y;
    uint16_t cover_margin;

    if ((diameter == 0U) ||
        (cover_diameter > diameter) ||
        (buffer == NULL) ||
        (image == NULL))
    {
        return SERVICE_INVALID_PARAM;
    }

    required_bytes = (uint32_t)diameter * (uint32_t)diameter * 3U;

    if (buffer_size < required_bytes)
    {
        return SERVICE_INVALID_PARAM;
    }

    disc_rgb565 = service_gui_canvas_rgb888_to_rgb565(disc_rgb);
    cover_rgb565 = service_gui_canvas_rgb888_to_rgb565(cover_rgb);
    cover_margin = (uint16_t)((diameter - cover_diameter) / 2U);

    disc_region.Area.x1 = 0;
    disc_region.Area.y1 = 0;
    disc_region.Area.x2 = (lv_coord_t)(diameter - 1U);
    disc_region.Area.y2 = (lv_coord_t)(diameter - 1U);
    disc_region.Radius = 0U;
    disc_region.Shape = SERVICE_GUI_CANVAS_REGION_CIRCLE;

    cover_region.Area.x1 = (lv_coord_t)cover_margin;
    cover_region.Area.y1 = (lv_coord_t)cover_margin;
    cover_region.Area.x2 =
        (lv_coord_t)((uint16_t)(cover_margin + cover_diameter) - 1U);
    cover_region.Area.y2 =
        (lv_coord_t)((uint16_t)(cover_margin + cover_diameter) - 1U);
    cover_region.Radius = 0U;
    cover_region.Shape = SERVICE_GUI_CANVAS_REGION_CIRCLE;

    for (y = 0; y < (int32_t)diameter; y++)
    {
        int32_t x;

        for (x = 0; x < (int32_t)diameter; x++)
        {
            if (!service_gui_canvas_is_inside_circle(&disc_region, x, y))
            {
                service_gui_canvas_write_true_color_alpha_pixel(
                    buffer,
                    (uint32_t)diameter,
                    x,
                    y,
                    0U,
                    0U);
                continue;
            }

            if ((cover_diameter != 0U) &&
                service_gui_canvas_is_inside_circle(&cover_region, x, y))
            {
                service_gui_canvas_write_true_color_alpha_pixel(
                    buffer,
                    (uint32_t)diameter,
                    x,
                    y,
                    cover_rgb565,
                    255U);
            }
            else
            {
                service_gui_canvas_write_true_color_alpha_pixel(
                    buffer,
                    (uint32_t)diameter,
                    x,
                    y,
                    disc_rgb565,
                    255U);
            }
        }
    }

    memset(image, 0, sizeof(*image));
    image->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    image->header.w = diameter;
    image->header.h = diameter;
    image->data_size = required_bytes;
    image->data = buffer;

    return SERVICE_OK;
}
