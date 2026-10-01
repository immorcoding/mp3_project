/**
  ******************************************************************************
  * @file    gui_service_view_music.c
  * @brief   Music 页：Playing / Queue / Library 标签视图及其静态内容。
  *
  * @details
  *          标签只能点击切换；内部 Content 不可滚动，横滑交给 Main 分页视口。
  *          Now Playing 含唱盘 Image 与控制区（时间、进度条、三键），事件由
  *          main/transport 绑定；Queue 行由 main/queue 按 Length 运行时构造。
  ******************************************************************************
  */

#include "Service/gui/view/gui_service_view_screens.h"

#include "Service/gui/theme/gui_service_theme_config.h"

#define SERVICE_GUI_VIEW_MUSIC_TAB_BAR_HEIGHT  (20)   /* 顶部标签栏高度，像素。 */
#define SERVICE_GUI_VIEW_MUSIC_VINYL_SIZE      (144)  /* 唱盘 Image 边长；须等于 SERVICE_GUI_MUSIC_VINYL_DIAMETER。 */

/**
 * @brief 设置标签栏（按钮矩阵）样式：薄 Wash 底、顶边细线；选中项 Accent 文字加底边线。
 * @param[in] buttons Tabview 的按钮矩阵。
 */
static void service_gui_view_music_style_tab_buttons(lv_obj_t *buttons)
{
    lv_obj_set_style_text_color(buttons, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_INK),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(buttons, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(buttons, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(buttons, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_WASH),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(buttons, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(buttons, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_WASH),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(buttons, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(buttons, LV_BORDER_SIDE_TOP, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_text_color(buttons, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_INK),
                                LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(buttons, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(buttons, &lv_font_montserrat_12, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(buttons, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_DEFAULT);

    lv_obj_set_style_text_color(buttons, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_ACCENT),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_opa(buttons, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(buttons, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(buttons, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_ACCENT),
                                  LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(buttons, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(buttons, 2, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(buttons, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS | LV_STATE_CHECKED);
}

/**
 * @brief 让 Tabview 内部 Content 透明且不可滚动。
 * @param[in] tabs 标签视图。
 * @note basic theme 会给 Content 白底；关掉滚动后内容区横滑只交给 Main 分页视口。
 */
static void service_gui_view_music_style_tab_content(lv_obj_t *tabs)
{
    lv_obj_t *content = lv_tabview_get_content(tabs);

    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_opa(content, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(content, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(content, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief 添加一页透明标签页（含透明滚动条）。
 * @param[in] tabs 标签视图。
 * @param[in] name 标签文字。
 */
static lv_obj_t *service_gui_view_music_add_tab(lv_obj_t *tabs, const char *name)
{
    lv_obj_t *tab = lv_tabview_add_tab(tabs, name);

    lv_obj_set_style_bg_color(tab, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(tab, lv_color_hex(0xFFFFFF), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    return tab;
}

/**
 * @brief 创建圆形半透明 Wash 控制按钮及其居中符号。
 * @param[in] parent 控制区。
 * @param[in] size 直径。
 * @param[in] x 相对控制区中心的水平偏移（百分比）。
 * @param[in] y 相对控制区中心的垂直偏移（百分比）。
 * @param[in] symbol LVGL 符号。
 * @param[out] icon 符号 Label；可为 NULL。
 */
static lv_obj_t *service_gui_view_music_create_control(lv_obj_t *parent,
                                                       lv_coord_t size,
                                                       lv_coord_t x,
                                                       lv_coord_t y,
                                                       const char *symbol,
                                                       lv_obj_t **icon)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_t *label;

    lv_obj_set_size(button, size, size);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_align(button, LV_ALIGN_CENTER);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_WASH),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_transform_zoom(button, LV_IMG_ZOOM_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_WASH),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, 80, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_transform_zoom(button, LV_IMG_ZOOM_NONE, LV_PART_MAIN | LV_STATE_PRESSED);

    label = lv_label_create(button);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_align(label, LV_ALIGN_CENTER);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_INK),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (icon != NULL)
    {
        *icon = label;
    }

    return button;
}

/**
 * @brief 创建 Now Playing：唱盘 Image 与底部控制区。
 * @param[out] music Music 页句柄。
 * @param[in] tab Now Playing 标签页。
 */
static void service_gui_view_music_create_now_playing(Service_GUI_ViewMusicTypeDef *music, lv_obj_t *tab)
{
    lv_obj_t *controls;
    lv_obj_t *label;
    lv_obj_t *slider;
    lv_obj_t *icon;

    lv_obj_set_style_text_color(tab, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_INK),
                                LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_opa(tab, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);

    music->vinyl_image = lv_img_create(tab);
    lv_obj_set_size(music->vinyl_image, SERVICE_GUI_VIEW_MUSIC_VINYL_SIZE, SERVICE_GUI_VIEW_MUSIC_VINYL_SIZE);
    lv_obj_set_pos(music->vinyl_image, 0, 15);
    lv_obj_set_align(music->vinyl_image, LV_ALIGN_TOP_MID);
    lv_obj_add_flag(music->vinyl_image, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(music->vinyl_image, LV_OBJ_FLAG_SCROLLABLE);

    controls = lv_obj_create(tab);
    lv_obj_remove_style_all(controls);
    lv_obj_set_size(controls, lv_pct(90), lv_pct(34));
    lv_obj_set_align(controls, LV_ALIGN_BOTTOM_MID);
    lv_obj_clear_flag(controls, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_hor(controls, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(controls, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    label = lv_label_create(controls);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(label, 0, lv_pct(5));
    lv_obj_set_align(label, LV_ALIGN_TOP_MID);
    lv_label_set_text(label, "1:00/3:14");
    lv_obj_set_style_text_color(label, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_INK),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Muted 轨道 + Wash 细边；Accent 进度；Knob 只在按下时显示并放大。 */
    slider = lv_slider_create(controls);
    lv_slider_set_value(slider, 0, LV_ANIM_OFF);
    lv_obj_set_size(slider, lv_pct(90), lv_pct(7));
    lv_obj_set_pos(slider, lv_pct(0), lv_pct(20));
    lv_obj_set_align(slider, LV_ALIGN_TOP_MID);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_MUTED),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, 150, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(slider, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_WASH),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(slider, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(slider, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(slider, 1000, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_ACCENT),
                              LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(slider, 1000, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_ACCENT),
                              LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(slider, 1000, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(slider, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_ACCENT),
                              LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(slider, 4, LV_PART_KNOB | LV_STATE_PRESSED);
    music->slider = slider;

    music->previous_button = service_gui_view_music_create_control(
        controls, 35, lv_pct(-30), lv_pct(19), LV_SYMBOL_PREV, &icon);
    lv_obj_set_pos(icon, lv_pct(0), 1);
    music->next_button = service_gui_view_music_create_control(
        controls, 35, lv_pct(30), lv_pct(19), LV_SYMBOL_NEXT, &icon);
    lv_obj_set_pos(icon, lv_pct(0), 1);
    music->play_pause_button = service_gui_view_music_create_control(
        controls, 44, lv_pct(0), lv_pct(18), LV_SYMBOL_PLAY, &music->play_pause_icon);
    lv_obj_set_style_text_font(music->play_pause_icon, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * @brief 创建 Queue 标签页：竖向 Flex 列表容器，行由 main/queue 运行时构造。
 * @param[in] tab Queue 标签页。
 */
static void service_gui_view_music_style_queue(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(tab, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(tab, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(tab, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(tab, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * @brief 在 Music 页中创建标签视图，并填写 Music 句柄。
 * @param[out] view 句柄集合。
 * @param[in] page Music 内容页。
 */
void service_gui_view_music_create(Service_GUI_ViewTypeDef *view, lv_obj_t *page)
{
    Service_GUI_ViewMusicTypeDef *music = &view->music;
    lv_obj_t *tabs;

    tabs = lv_tabview_create(page, LV_DIR_TOP, SERVICE_GUI_VIEW_MUSIC_TAB_BAR_HEIGHT);
    lv_obj_set_size(tabs, lv_pct(100), lv_pct(100));
    lv_obj_set_align(tabs, LV_ALIGN_TOP_MID);
    lv_obj_clear_flag(tabs, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(tabs, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_WASH),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_view_music_style_tab_buttons(lv_tabview_get_tab_btns(tabs));
    service_gui_view_music_style_tab_content(tabs);

    service_gui_view_music_create_now_playing(music, service_gui_view_music_add_tab(tabs, "Playing"));
    music->queue_tab = service_gui_view_music_add_tab(tabs, "Queue");
    service_gui_view_music_style_queue(music->queue_tab);
    (void)service_gui_view_music_add_tab(tabs, "Library");

    music->tabs = tabs;
}
