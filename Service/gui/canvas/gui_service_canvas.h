/**
  ******************************************************************************
  * @file    gui_service_canvas.h
  * @brief   GUI Service 私有 Canvas 离屏处理 Interface。
  *
 * @details
 *          该头仅供 Service/gui 内部 Module 调用。它隐藏 Canvas 工作区、SDRAM
 *          放置、对象生命周期和 LVGL 图像格式校验；不属于 Service 层的公开
 *          Interface。全屏模糊工作帧仍是可复用临时缓冲；唱盘缓冲独立，可长期
 *          绑到 Image。
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

Service_StatusTypeDef service_gui_canvas_compose_music_vinyl(
    const lv_img_dsc_t *base_image,
    lv_img_dsc_t **image);

#endif /* GUI_SERVICE_CANVAS_H */
