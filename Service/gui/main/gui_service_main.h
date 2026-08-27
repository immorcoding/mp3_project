/**
  ******************************************************************************
  * @file    gui_service_main.h
  * @brief   GUI Service 私有 Main Screen 运行时视觉 Interface。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_MAIN_H
#define GUI_SERVICE_MAIN_H

#include "Service/service.h"

#include "lvgl.h"

/**
 * @brief 为 Main Screen 生成并绑定当前壁纸的局部毛玻璃合成背景。
 * @param clear_wallpaper 当前清晰系统壁纸。
 * @retval SERVICE_OK Main 运行时背景、Tabview 内部 Content 兼容处理和局部玻璃合成均已完成。
 * @retval SERVICE_NOT_READY SquareLine 的 Main 或目标控件尚未就绪。
 * @retval SERVICE_INVALID_PARAM 壁纸格式或区域不满足 Canvas 合成约束。
 * @note 除 ui_Main 的运行时 Background image 外，不覆盖 SquareLine 导出对象的
 *       背景、边框、阴影、圆角或文字设计。
 */
Service_StatusTypeDef service_gui_main_prepare_background(
    const lv_img_dsc_t *clear_wallpaper);

#endif /* GUI_SERVICE_MAIN_H */
