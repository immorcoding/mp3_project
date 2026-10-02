/**
  ******************************************************************************
  * @file    gui_service_view_boot.c
  * @brief   Boot 与 BootReveal Screen 的对象树和静态样式。
  *
  * @details
  *          Boot 以壁纸为根背景，中间是启动环与 LOADING 文字；BootReveal 只承载
  *          清晰壁纸。两者之间以及到 Lock 的切换时序由 boot/ 负责。
  ******************************************************************************
  */

#include "Service/gui/view/gui_service_view_screens.h"

#include "Service/gui/theme/gui_service_theme_style.h"

/**
 * @brief 创建一个以壁纸为根背景、不可滚动的 Screen。
 * @param[in] wallpaper 根背景图。
 * @return 新 Screen。
 */
static lv_obj_t *service_gui_view_boot_create_wallpaper_screen(const lv_img_dsc_t *wallpaper)
{
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    service_gui_theme_style_add(screen, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_GROUND, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(screen, wallpaper, LV_PART_MAIN | LV_STATE_DEFAULT);
    return screen;
}

/**
 * @brief 创建 Boot 与 BootReveal，并填写其句柄。
 * @param[out] view 句柄集合。
 * @param[in] wallpaper 清晰系统壁纸。
 */
void service_gui_view_boot_create(Service_GUI_ViewTypeDef *view, const lv_img_dsc_t *wallpaper)
{
    lv_obj_t *boot;
    lv_obj_t *group;
    lv_obj_t *ring;
    lv_obj_t *label;

    boot = service_gui_view_boot_create_wallpaper_screen(wallpaper);
    lv_obj_set_style_bg_img_opa(boot, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    group = lv_obj_create(boot);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, lv_pct(66), lv_pct(66));
    lv_obj_set_pos(group, lv_pct(0), lv_pct(-5));
    lv_obj_set_align(group, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* 轨道为 Muted 满圈，Indicator 为 Accent，由 boot/ 驱动相位动画。 */
    ring = lv_arc_create(group);
    lv_obj_set_size(ring, lv_pct(33), lv_pct(30));
    lv_obj_set_pos(ring, 14, 4);
    lv_obj_set_align(ring, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ring, LV_DIR_TOP);
    lv_arc_set_value(ring, 50);
    lv_arc_set_bg_angles(ring, 0, 360);
    service_gui_theme_style_add(ring, SERVICE_GUI_THEME_ARC, SERVICE_GUI_THEME_MUTED, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ring, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ring, 2, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    service_gui_theme_style_add(ring, SERVICE_GUI_THEME_ARC, SERVICE_GUI_THEME_ACCENT, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ring, 6, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);

    label = lv_label_create(group);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_align(label, LV_ALIGN_CENTER);
    lv_label_set_text(label, "LOADING");
    service_gui_theme_style_add(label, SERVICE_GUI_THEME_TEXT, SERVICE_GUI_THEME_INK, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    view->boot.screen = boot;
    view->boot.orbit_ring = ring;
    view->boot_reveal = service_gui_view_boot_create_wallpaper_screen(wallpaper);
}
