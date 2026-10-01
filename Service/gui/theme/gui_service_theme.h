/**
  ******************************************************************************
  * @file    gui_service_theme.h
  * @brief   GUI Service 私有调色板：角色色值与当前外观索引。
  *
  * @details
  *          本头不含 LVGL，主机测试只编译调色板 Implementation。共享 LVGL style
  *          见同目录 gui_service_theme_style.h。APP 不得包含本文件。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_THEME_H
#define GUI_SERVICE_THEME_H

#include <stdbool.h>
#include <stdint.h>

#include "Service/gui/gui_service.h"
#include "Service/service.h"

/** @brief 调色板角色；界面对象只引用角色，不写死 RGB。 */
typedef enum
{
    SERVICE_GUI_THEME_ACCENT = 0U,  /**< 进度、当前行、Tab 选中、电池填充。 */
    SERVICE_GUI_THEME_INK,          /**< 主文字、浅轮廓、图标。 */
    SERVICE_GUI_THEME_MUTED,        /**< 非活动轨道等低对比元素。 */
    SERVICE_GUI_THEME_WASH,         /**< 按钮、Queue 行等薄填充；Opa 写在对象上。 */
    SERVICE_GUI_THEME_GROUND,       /**< 纯色页底（无壁纸外观）。 */
    SERVICE_GUI_THEME_ROLE_COUNT    /**< 角色数量，不是角色。 */
} Service_GUI_ThemeRoleTypeDef;

uint8_t service_gui_theme_get_current(void);
Service_StatusTypeDef service_gui_theme_select(uint8_t id);
uint32_t service_gui_theme_role_color(Service_GUI_ThemeRoleTypeDef role);
bool service_gui_theme_uses_wallpaper(void);
bool service_gui_theme_uses_glass(void);

#endif /* GUI_SERVICE_THEME_H */
