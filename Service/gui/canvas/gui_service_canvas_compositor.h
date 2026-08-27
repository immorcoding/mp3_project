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
    /** @brief 使用 Radius 描述的圆角矩形区域；Radius 为零时等价于矩形。 */
    SERVICE_GUI_CANVAS_REGION_ROUNDED_RECT = 0U,
    /** @brief 以区域短边为直径、中心与区域中心重合的内接圆区域。 */
    SERVICE_GUI_CANVAS_REGION_CIRCLE
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

/**
 * @brief 将清晰全屏图片和同格式模糊帧合成为长期背景图片。
 * @param clear_image 清晰图片；当前原型只支持 LV_IMG_CF_TRUE_COLOR_ALPHA。
 * @param blurred_image 同尺寸、同格式、包含完整 data_size 的模糊图片。
 * @param regions 需要替换为模糊像素的区域数组；region_count 为零时可为 NULL。
 * @param region_count 区域数量。
 * @param composite_buffer 调用方持有且至少容纳一帧数据的输出缓冲。
 * @param composite_buffer_size 输出缓冲大小，单位为字节。
 * @param composite_image 返回绑定 composite_buffer 的输出描述符。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_INVALID_PARAM 图片格式、尺寸、区域或输出缓冲不满足约束。
 */
Service_StatusTypeDef service_gui_canvas_compose_blurred_regions(
    const lv_img_dsc_t *clear_image,
    const lv_img_dsc_t *blurred_image,
    const Service_GUI_CanvasBlurRegionTypeDef *regions,
    uint32_t region_count,
    uint8_t *composite_buffer,
    uint32_t composite_buffer_size,
    lv_img_dsc_t *composite_image);

#endif /* GUI_SERVICE_CANVAS_COMPOSITOR_H */
