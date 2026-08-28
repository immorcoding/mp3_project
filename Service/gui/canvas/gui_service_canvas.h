/**
  ******************************************************************************
  * @file    gui_service_canvas.h
  * @brief   GUI Service 私有 Canvas 离屏处理 Interface。
  *
 * @details
 *          该头仅供 Service/gui 内部 Module 调用。它隐藏 Canvas 工作区、SDRAM
 *          放置、对象生命周期和 LVGL 图像格式校验；不属于 Service 层的公开
 *          Interface。Canvas 只提供一份可复用的临时全屏工作帧；调用方若要在
 *          后续帧或事件中继续读取模糊像素，必须复制到自己长期持有的缓冲。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_CANVAS_H
#define GUI_SERVICE_CANVAS_H

#include <stdint.h>

#include "Service/service.h"

#include "lvgl.h"

Service_StatusTypeDef service_gui_canvas_blur_image(
    const lv_img_dsc_t *source,
    uint16_t blur_radius,
    lv_img_dsc_t **blurred_image);

#endif /* GUI_SERVICE_CANVAS_H */
