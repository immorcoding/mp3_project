/**
  ******************************************************************************
  * @file    gui_service_theme_style.c
  * @brief   共享颜色 style 与 Screen 壁纸/Ground 外观。
  *
  * @details
  *          共 5 × 5 个只含一项颜色的 lv_style_t。对象按角色引用它们，因此切外观只需
  *          改这些 style 的颜色并调用 lv_obj_report_style_change()。Screen 根对象始终
  *          挂 Ground 背景色；有壁纸的外观把背景色打透明并显示壁纸，Solid 反之。
  ******************************************************************************
  */

#include "Service/gui/theme/gui_service_theme_style.h"

#include <stdbool.h>

static lv_style_t service_gui_theme_styles[SERVICE_GUI_THEME_PROP_COUNT][SERVICE_GUI_THEME_ROLE_COUNT];
static bool service_gui_theme_styles_ready;

/**
 * @brief 按当前调色板写入一个共享 style 的颜色。
 * @param[in] prop 颜色属性。
 * @param[in] role 调色板角色。
 */
static void service_gui_theme_style_write(Service_GUI_ThemePropTypeDef prop, Service_GUI_ThemeRoleTypeDef role)
{
    lv_style_t *style = &service_gui_theme_styles[prop][role];
    const lv_color_t color = lv_color_hex(service_gui_theme_role_color(role));

    switch (prop)
    {
    case SERVICE_GUI_THEME_TEXT:
        lv_style_set_text_color(style, color);
        break;
    case SERVICE_GUI_THEME_BG:
        lv_style_set_bg_color(style, color);
        break;
    case SERVICE_GUI_THEME_BORDER:
        lv_style_set_border_color(style, color);
        break;
    case SERVICE_GUI_THEME_OUTLINE:
        lv_style_set_outline_color(style, color);
        break;
    case SERVICE_GUI_THEME_ARC:
        lv_style_set_arc_color(style, color);
        break;
    default:
        break;
    }
}

/**
 * @brief 按当前调色板写入全部共享 style 的颜色。
 */
static void service_gui_theme_style_write_all(void)
{
    uint32_t prop;
    uint32_t role;

    for (prop = 0U; prop < (uint32_t)SERVICE_GUI_THEME_PROP_COUNT; prop++)
    {
        for (role = 0U; role < (uint32_t)SERVICE_GUI_THEME_ROLE_COUNT; role++)
        {
            service_gui_theme_style_write((Service_GUI_ThemePropTypeDef)prop,
                                          (Service_GUI_ThemeRoleTypeDef)role);
        }
    }
}

/**
 * @brief 以当前外观初始化共享 style。
 * @note 必须在 view/ 创建对象之前由 GUI Task 调用；重复调用无副作用。
 */
void service_gui_theme_style_init(void)
{
    uint32_t prop;
    uint32_t role;

    if (service_gui_theme_styles_ready)
    {
        return;
    }

    for (prop = 0U; prop < (uint32_t)SERVICE_GUI_THEME_PROP_COUNT; prop++)
    {
        for (role = 0U; role < (uint32_t)SERVICE_GUI_THEME_ROLE_COUNT; role++)
        {
            lv_style_init(&service_gui_theme_styles[prop][role]);
        }
    }

    service_gui_theme_style_write_all();
    service_gui_theme_styles_ready = true;
}

/**
 * @brief 给对象的指定 part/state 挂上某角色的颜色。
 * @param[in,out] obj 目标对象。
 * @param[in] prop 颜色属性。
 * @param[in] role 调色板角色。
 * @param[in] selector part 与 state。
 */
void service_gui_theme_style_add(lv_obj_t *obj,
                                 Service_GUI_ThemePropTypeDef prop,
                                 Service_GUI_ThemeRoleTypeDef role,
                                 lv_style_selector_t selector)
{
    if ((obj == NULL) || (prop >= SERVICE_GUI_THEME_PROP_COUNT) || (role >= SERVICE_GUI_THEME_ROLE_COUNT))
    {
        return;
    }

    lv_obj_add_style(obj, &service_gui_theme_styles[prop][role], selector);
}

/**
 * @brief 把对象某 part/state 上该属性的角色换成另一个角色。
 * @param[in,out] obj 目标对象。
 * @param[in] prop 颜色属性。
 * @param[in] role 新角色。
 * @param[in] selector part 与 state。
 * @note 用于运行时切换当前/非当前等状态色，例如 Queue 当前行曲名。
 */
void service_gui_theme_style_replace(lv_obj_t *obj,
                                     Service_GUI_ThemePropTypeDef prop,
                                     Service_GUI_ThemeRoleTypeDef role,
                                     lv_style_selector_t selector)
{
    uint32_t other;

    if ((obj == NULL) || (prop >= SERVICE_GUI_THEME_PROP_COUNT) || (role >= SERVICE_GUI_THEME_ROLE_COUNT))
    {
        return;
    }

    for (other = 0U; other < (uint32_t)SERVICE_GUI_THEME_ROLE_COUNT; other++)
    {
        if (other != (uint32_t)role)
        {
            lv_obj_remove_style(obj, &service_gui_theme_styles[prop][other], selector);
        }
    }

    lv_obj_remove_style(obj, &service_gui_theme_styles[prop][role], selector);
    lv_obj_add_style(obj, &service_gui_theme_styles[prop][role], selector);
}

/**
 * @brief 按当前外观设置 Screen 根对象：壁纸或 Ground。
 * @param[in,out] screen Screen；为 NULL（例如 Boot 已删除）时忽略。
 */
static void service_gui_theme_style_apply_screen(lv_obj_t *screen)
{
    if (screen == NULL)
    {
        return;
    }

    if (service_gui_theme_uses_wallpaper())
    {
        lv_obj_set_style_bg_img_opa(screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    else
    {
        lv_obj_set_style_bg_img_opa(screen, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    lv_obj_invalidate(screen);
}

/**
 * @brief 把当前外观应用到全部共享 style 与各 Screen 根对象。
 * @param[in] view 界面句柄。
 * @note 只能由 GUI Task 在 service_gui_theme_select() 之后调用。Music Tab 的
 *       毛玻璃/薄层由 main/background 负责。
 */
void service_gui_theme_style_apply(const Service_GUI_ViewTypeDef *view)
{
    if (!service_gui_theme_styles_ready)
    {
        return;
    }

    service_gui_theme_style_write_all();
    lv_obj_report_style_change(NULL);

    service_gui_theme_style_apply_screen(view->boot.screen);
    service_gui_theme_style_apply_screen(view->boot_reveal);
    service_gui_theme_style_apply_screen(view->lock);
    service_gui_theme_style_apply_screen(view->main.screen);
}
