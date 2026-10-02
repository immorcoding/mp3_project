/**
  ******************************************************************************
  * @file    gui_service_view_main.c
  * @brief   Main Screen 外壳：状态栏、横向分页视口、三张内容页与分页圆点。
  *
  * @details
  *          纵向分区：状态栏 5%、分页视口 85%、圆点 10%。三张内容页初始位于
  *          视口的 0% / 100% / 200% 槽位（Settings / Music / Books）；吸附、循环
  *          与圆点动画由 main/pager 负责，Music 页内容由 gui_service_view_music.c 构建。
  ******************************************************************************
  */

#include "Service/gui/view/gui_service_view_screens.h"

#include "Service/gui/theme/gui_service_theme_style.h"

/**
 * @brief 创建一个去除主题样式、不可点击不可滚动的透明容器。
 * @param[in] parent 父对象。
 * @param[in] width 宽度。
 * @param[in] height 高度。
 */
static lv_obj_t *service_gui_view_main_create_box(lv_obj_t *parent, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *box = lv_obj_create(parent);

    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, width, height);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

/**
 * @brief 创建状态栏用的 10 px Ink 文字。
 * @param[in] parent 父对象。
 * @param[in] text 文本。
 */
static lv_obj_t *service_gui_view_main_create_status_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_label_set_text(label, text);
    service_gui_theme_style_add(label, SERVICE_GUI_THEME_TEXT, SERVICE_GUI_THEME_INK, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
    return label;
}

/**
 * @brief 创建顶部状态栏：时间、日期、电量。
 * @param[in] screen Main Screen。
 */
static void service_gui_view_main_create_status_bar(lv_obj_t *screen)
{
    lv_obj_t *bar_container;
    lv_obj_t *box;
    lv_obj_t *battery_box;
    lv_obj_t *battery;
    lv_obj_t *label;

    bar_container = service_gui_view_main_create_box(screen, lv_pct(100), lv_pct(5));
    lv_obj_set_align(bar_container, LV_ALIGN_TOP_MID);
    lv_obj_set_flex_flow(bar_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    box = service_gui_view_main_create_box(bar_container, lv_pct(20), lv_pct(90));
    lv_obj_set_align(box, LV_ALIGN_CENTER);
    label = service_gui_view_main_create_status_label(box, "10:42");
    lv_obj_set_align(label, LV_ALIGN_LEFT_MID);

    box = service_gui_view_main_create_box(bar_container, lv_pct(50), lv_pct(90));
    lv_obj_set_align(box, LV_ALIGN_CENTER);
    label = service_gui_view_main_create_status_label(box, "AUG 24");
    lv_obj_set_align(label, LV_ALIGN_CENTER);

    box = service_gui_view_main_create_box(bar_container, lv_pct(20), lv_pct(90));
    lv_obj_set_pos(box, lv_pct(0), lv_pct(4));
    lv_obj_set_align(box, LV_ALIGN_CENTER);
    lv_obj_set_style_pad_all(box, 1, LV_PART_MAIN | LV_STATE_DEFAULT);

    battery_box = service_gui_view_main_create_box(box, lv_pct(50), lv_pct(100));
    lv_obj_set_align(battery_box, LV_ALIGN_RIGHT_MID);

    /* 电池胶囊：Ink 轮廓，Accent 填充，百分比数字叠在中间。 */
    battery = lv_bar_create(battery_box);
    lv_bar_set_value(battery, 84, LV_ANIM_OFF);
    lv_bar_set_start_value(battery, 0, LV_ANIM_OFF);
    lv_obj_set_size(battery, lv_pct(90), lv_pct(90));
    lv_obj_set_align(battery, LV_ALIGN_CENTER);
    lv_obj_clear_flag(battery, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(battery, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(battery, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(battery, SERVICE_GUI_THEME_OUTLINE, SERVICE_GUI_THEME_INK, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_opa(battery, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(battery, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_pad(battery, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(battery, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(battery, 2, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    service_gui_theme_style_add(battery, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_ACCENT, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(battery, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    label = service_gui_view_main_create_status_label(battery_box, "84");
    lv_obj_set_align(label, LV_ALIGN_CENTER);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief 在分页视口中创建一张透明内容页。
 * @param[in] container 分页视口。
 * @param[in] slot_x 初始槽位水平偏移（百分比）。
 */
static lv_obj_t *service_gui_view_main_create_page(lv_obj_t *container, lv_coord_t slot_x)
{
    lv_obj_t *page = service_gui_view_main_create_box(container, lv_pct(100), lv_pct(100));

    lv_obj_set_pos(page, slot_x, 0);
    lv_obj_set_align(page, LV_ALIGN_TOP_MID);
    lv_obj_set_scroll_dir(page, LV_DIR_HOR);
    return page;
}

/**
 * @brief 创建一个分页圆点。
 * @param[in] parent 圆点容器。
 * @param[in] width 宽度；活动页为胶囊宽度。
 * @param[in] opa 背景不透明度；活动页更亮。
 */
static lv_obj_t *service_gui_view_main_create_dot(lv_obj_t *parent, lv_coord_t width, lv_opa_t opa)
{
    lv_obj_t *dot = lv_obj_create(parent);

    lv_obj_set_size(dot, width, 5);
    lv_obj_set_pos(dot, -41, 137);
    lv_obj_set_align(dot, LV_ALIGN_CENTER);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(dot, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(dot, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_INK, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(dot, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
    return dot;
}

/**
 * @brief 创建 Main Screen，并填写外壳与 Music 页句柄。
 * @param[out] view 句柄集合。
 * @param[in] wallpaper 清晰系统壁纸。
 */
void service_gui_view_main_create(Service_GUI_ViewTypeDef *view, const lv_img_dsc_t *wallpaper)
{
    lv_obj_t *screen;
    lv_obj_t *container;
    lv_obj_t *dots;

    screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    service_gui_theme_style_add(screen, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_GROUND, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(screen, wallpaper, LV_PART_MAIN | LV_STATE_DEFAULT);

    service_gui_view_main_create_status_bar(screen);

    /* 唯一的横向滚动视口：保留 Clickable 才能被指针命中；关闭惯性与弹性，吸附由 pager 决定。 */
    container = lv_obj_create(screen);
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, lv_pct(100), lv_pct(85));
    lv_obj_set_pos(container, lv_pct(0), lv_pct(5));
    lv_obj_set_align(container, LV_ALIGN_TOP_MID);
    lv_obj_clear_flag(container,
                      LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(container, LV_DIR_HOR);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(container, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 子对象顺序决定绘制与命中次序：Music、Books、Settings。 */
    view->main.music_page = service_gui_view_main_create_page(container, lv_pct(100));
    lv_obj_set_style_pad_hor(view->main.music_page, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(view->main.music_page, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_view_music_create(view, view->main.music_page);
    view->main.books_page = service_gui_view_main_create_page(container, lv_pct(200));
    view->main.settings_page = service_gui_view_main_create_page(container, lv_pct(0));

    dots = service_gui_view_main_create_box(screen, lv_pct(100), lv_pct(10));
    lv_obj_set_align(dots, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(dots, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(dots, 5, LV_PART_MAIN | LV_STATE_DEFAULT);

    view->main.dot_settings = service_gui_view_main_create_dot(dots, 5, 180);
    view->main.dot_music = service_gui_view_main_create_dot(dots, 14, 220);
    view->main.dot_books = service_gui_view_main_create_dot(dots, 5, 180);

    view->main.screen = screen;
    view->main.page_container = container;
}
