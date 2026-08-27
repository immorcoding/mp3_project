/**
  ******************************************************************************
  * @file    gui_service_boot.h
  * @brief   GUI Service 私有启动视觉序列 Interface。
  *
  * @details
  *          该头仅供 Service/gui 的生命周期 Module 调用。它不暴露给 APP 或
  *          其他 Service；它收纳 Boot 背景准备、Arc 动画，以及由 SquareLine
  *          启动页事件触发的异步切屏交接。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_BOOT_H
#define GUI_SERVICE_BOOT_H

#include "Service/service.h"

#include "lvgl.h"

/**
 * @brief 为 Boot 根对象绑定当前壁纸的运行时模糊背景。
 * @param clear_wallpaper 当前系统壁纸的清晰图片描述符。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY Boot 或共享 Canvas 工作对象尚未就绪。
 * @retval SERVICE_INVALID_PARAM 壁纸格式、尺寸或数据长度不满足 Canvas 约束。
 */
Service_StatusTypeDef service_gui_boot_prepare_background(
    const lv_img_dsc_t *clear_wallpaper);

/**
 * @brief 启动 SquareLine Boot Arc 的运行时循环动画。
 * @note 调用前必须完成 ui_init() 与 service_gui_boot_prepare_background()。
 */
void service_gui_boot_start(void);

#endif /* GUI_SERVICE_BOOT_H */
