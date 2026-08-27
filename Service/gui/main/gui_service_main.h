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
 * @brief 为 Main Screen 的 MusicModeTabs 构建并绑定实时局部毛玻璃。
 * @param clear_wallpaper 当前清晰系统壁纸。
 * @retval SERVICE_OK Tabview 内部 Content 兼容处理和 MusicModeTabs 局部背景均已完成。
 * @retval SERVICE_NOT_READY SquareLine 的 Main 或目标控件尚未就绪。
 * @retval SERVICE_INVALID_PARAM 壁纸格式、区域或裁剪缓冲不满足 Canvas 约束。
 * @note 仅为 ui_MusicModeTabs 绑定运行时 Background image，并在 MainPager 滑动
 *       时按其当前坐标更新图像像素；不覆盖任何 SquareLine 导出对象的边框、阴影、
 *       圆角、文字或其他视觉设计。
 */
Service_StatusTypeDef service_gui_main_prepare_background(
    const lv_img_dsc_t *clear_wallpaper);

#endif /* GUI_SERVICE_MAIN_H */
