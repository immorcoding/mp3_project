/**
  ******************************************************************************
  * @file    gui_service_theme_style.h
  * @brief   GUI 调色板的共享 LVGL style 与 Screen 外观副作用。
  *
  * @details
  *          每个（颜色属性 × 调色板角色）对应一个共享 lv_style_t，只含一项颜色。
  *          view/ 与 main/queue 用本头把角色挂到对象上；Opa 等其他属性仍写在对象上。
  *          切外观时只刷新这些 style 的颜色并通知 LVGL，不扫对象改 style。
  *          APP 不得包含本文件。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_THEME_STYLE_H
#define GUI_SERVICE_THEME_STYLE_H

#include "Service/gui/theme/gui_service_theme.h"
#include "Service/gui/view/gui_service_view.h"

#include "lvgl.h"

/** @brief 共享 style 承载的颜色属性。 */
typedef enum
{
    SERVICE_GUI_THEME_TEXT = 0U,  /**< text_color。 */
    SERVICE_GUI_THEME_BG,         /**< bg_color。 */
    SERVICE_GUI_THEME_BORDER,     /**< border_color。 */
    SERVICE_GUI_THEME_OUTLINE,    /**< outline_color。 */
    SERVICE_GUI_THEME_ARC,        /**< arc_color。 */
    SERVICE_GUI_THEME_PROP_COUNT  /**< 属性数量，不是属性。 */
} Service_GUI_ThemePropTypeDef;

void service_gui_theme_style_init(void);
void service_gui_theme_style_add(lv_obj_t *obj,
                                 Service_GUI_ThemePropTypeDef prop,
                                 Service_GUI_ThemeRoleTypeDef role,
                                 lv_style_selector_t selector);
void service_gui_theme_style_replace(lv_obj_t *obj,
                                     Service_GUI_ThemePropTypeDef prop,
                                     Service_GUI_ThemeRoleTypeDef role,
                                     lv_style_selector_t selector);
void service_gui_theme_style_apply(const Service_GUI_ViewTypeDef *view);

#endif /* GUI_SERVICE_THEME_STYLE_H */
