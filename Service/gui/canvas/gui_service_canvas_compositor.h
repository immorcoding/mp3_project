/**
  ******************************************************************************
  * @file    gui_service_canvas_compositor.h
  * @brief   GUI Service 私有局部毛玻璃合成 Interface。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_CANVAS_COMPOSITOR_H
#define GUI_SERVICE_CANVAS_COMPOSITOR_H

#include <stdint.h>

#include "Service/service.h"

#include "lvgl.h"

typedef enum
{
    SERVICE_GUI_CANVAS_REGION_ROUNDED_RECT = 0U, /**< 使用 Radius 描述的圆角矩形；Radius 为零时等价于矩形。 */
    SERVICE_GUI_CANVAS_REGION_CIRCLE             /**< 以区域短边为直径、中心与区域中心重合的内接圆。 */
} Service_GUI_CanvasRegionShapeTypeDef;

typedef struct
{
    /** @brief 以输入图片左上角为原点的闭区间像素区域。 */
    lv_area_t Area;
    /** @brief 圆角半径；仅在 SERVICE_GUI_CANVAS_REGION_ROUNDED_RECT 时生效。 */
    uint16_t Radius;
    /** @brief 当前区域的裁剪形状。 */
    Service_GUI_CanvasRegionShapeTypeDef Shape;
} Service_GUI_CanvasBlurRegionTypeDef;

Service_StatusTypeDef service_gui_canvas_extract_image_region(
    const lv_img_dsc_t *source_image,
    const lv_area_t *source_area,
    uint8_t *cropped_buffer,
    uint32_t cropped_buffer_size,
    lv_img_dsc_t *cropped_image);

Service_StatusTypeDef service_gui_canvas_extract_image_region_padded(
    const lv_img_dsc_t *source_image,
    const lv_area_t *source_area,
    uint8_t *cropped_buffer,
    uint32_t cropped_buffer_size,
    lv_img_dsc_t *cropped_image);

Service_StatusTypeDef service_gui_canvas_compose_blurred_regions(
    const lv_img_dsc_t *clear_image,
    const lv_img_dsc_t *blurred_image,
    const Service_GUI_CanvasBlurRegionTypeDef *regions,
    uint32_t region_count,
    uint8_t *composite_buffer,
    uint32_t composite_buffer_size,
    lv_img_dsc_t *composite_image);

#endif /* GUI_SERVICE_CANVAS_COMPOSITOR_H */
