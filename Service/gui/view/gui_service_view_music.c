/**
  ******************************************************************************
  * @file    gui_service_view_music.c
  * @brief   Music 页：Playing / Queue / Library 标签视图及其静态内容。
  *
  * @details
  *          标签只能点击切换；内部 Content 不可滚动，横滑交给 Main 分页视口。
  *          Now Playing 自上而下为唱盘、曲名、歌手、进度条、时间与三键，位置由
  *          gui_service_config.h 的一份像素常量配置推出（GUI-27）；事件由 main/transport
  *          绑定，曲名与歌手文本由 main/title 写入；Queue 行由 main/queue 按 Length 运行时构造。
  ******************************************************************************
  */

#include "Service/gui/view/gui_service_view_screens.h"

#include "Service/gui/gui_service.h"
#include "Service/gui/gui_service_config.h"
#include "Service/gui/font/gui_service_font.h"
#include "Service/gui/theme/gui_service_theme_style.h"

#define SERVICE_GUI_VIEW_MUSIC_TAB_BAR_HEIGHT  (20)   /* 顶部标签栏高度，像素。 */

/**
 * @brief 设置标签栏（按钮矩阵）样式：薄 Wash 底、顶边细线；选中项 Accent 文字加底边线。
 * @param[in] buttons Tabview 的按钮矩阵。
 */
static void service_gui_view_music_style_tab_buttons(lv_obj_t *buttons)
{
    service_gui_theme_style_add(buttons, SERVICE_GUI_THEME_TEXT, SERVICE_GUI_THEME_INK, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(buttons, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(buttons, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(buttons, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_WASH, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(buttons, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(buttons, SERVICE_GUI_THEME_BORDER, SERVICE_GUI_THEME_WASH, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(buttons, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(buttons, LV_BORDER_SIDE_TOP, LV_PART_MAIN | LV_STATE_DEFAULT);

    service_gui_theme_style_add(buttons, SERVICE_GUI_THEME_TEXT, SERVICE_GUI_THEME_INK, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(buttons, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(buttons, &lv_font_montserrat_12, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_DEFAULT);

    service_gui_theme_style_add(buttons, SERVICE_GUI_THEME_TEXT, SERVICE_GUI_THEME_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_opa(buttons, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_CHECKED);
    service_gui_theme_style_add(buttons, SERVICE_GUI_THEME_BORDER, SERVICE_GUI_THEME_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
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

    lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    return tab;
}

/** @brief Now Playing 推出的版面；Y 都相对 Playing 标签页内容区顶边（GUI-27）。 */
typedef struct
{
    lv_coord_t gap;            /**< 组间距 S。 */
    lv_coord_t disc;           /**< 唱盘可见直径，偶数。 */
    uint16_t zoom;             /**< 唱盘 Image 的 lv_img zoom（256 = 原尺寸）。 */
    lv_coord_t slider_height;  /**< 进度条高：配置值加上唱盘取偶数后余下的 1 行。 */
    lv_coord_t disc_top;       /**< 唱盘裁切容器上沿。 */
    lv_coord_t title_ink_top;  /**< 曲名完整字形上沿。 */
    lv_coord_t artist_ink_top; /**< 歌手完整字形上沿。 */
    lv_coord_t slider_top;     /**< 进度条上沿。 */
    lv_coord_t time_ink_top;   /**< 时间完整字形上沿。 */
    lv_coord_t button_top;     /**< 主键上沿；侧键上沿再下移 (主键 - 侧键) / 2，共用中线。 */
    lv_coord_t column_x;       /**< 横向内容列左沿：进度条、曲名、歌手与两侧键外沿共用。 */
} Service_GUI_ViewMusicLayoutTypeDef;

/**
 * @brief 由像素常量配置与内容区尺寸推出 Now Playing 的纵向版面。
 * @param[in] width Playing 标签页内容区宽度。
 * @param[in] height Playing 标签页内容区高度 H。
 * @return 推出的版面。
 * @note  4S + G + D + (2·文字字形高 + 曲名→歌手) + (进度条 + 进度条→时间 + 时间字形高) + 主键 = H，
 *        G 为歌手→进度条。S 从上限起按网格递减，取第一个使 D 不小于下限的值；到 S 下限
 *        仍不足时不再压缩 S，唱盘按推出值显示。D 取偶数（唱盘绕中心缩放，奇数直径左右
 *        不对称），余下的 1 行加给进度条。
 */
static Service_GUI_ViewMusicLayoutTypeDef service_gui_view_music_solve_layout(lv_coord_t width, lv_coord_t height)
{
    const lv_coord_t fixed = (lv_coord_t)(2 * SERVICE_GUI_MUSIC_LAYOUT_TEXT_INK_H +
                                          SERVICE_GUI_MUSIC_LAYOUT_TEXT_GAP +
                                          SERVICE_GUI_MUSIC_LAYOUT_TEXT_PROGRESS +
                                          SERVICE_GUI_MUSIC_LAYOUT_SLIDER_HEIGHT +
                                          SERVICE_GUI_MUSIC_LAYOUT_PROGRESS_GAP +
                                          SERVICE_GUI_MUSIC_LAYOUT_TIME_INK_H +
                                          SERVICE_GUI_MUSIC_LAYOUT_MAIN_BUTTON);
    Service_GUI_ViewMusicLayoutTypeDef layout;
    lv_coord_t slack;

    layout.gap = SERVICE_GUI_MUSIC_LAYOUT_GAP_MAX;
    layout.disc = (lv_coord_t)(height - 4 * layout.gap - fixed);
    while ((layout.disc < SERVICE_GUI_MUSIC_LAYOUT_DISC_MIN) &&
           (layout.gap - SERVICE_GUI_MUSIC_LAYOUT_GRID >= SERVICE_GUI_MUSIC_LAYOUT_GAP_MIN))
    {
        layout.gap = (lv_coord_t)(layout.gap - SERVICE_GUI_MUSIC_LAYOUT_GRID);
        layout.disc = (lv_coord_t)(height - 4 * layout.gap - fixed);
    }

    slack = (lv_coord_t)(layout.disc & 1);
    layout.disc = (lv_coord_t)(layout.disc - slack);
    if (layout.disc > (lv_coord_t)SERVICE_GUI_MUSIC_VINYL_DIAMETER)
    {
        /* 放大会糊：可见直径最多等于底图，多出的高度留在页底。 */
        layout.disc = (lv_coord_t)SERVICE_GUI_MUSIC_VINYL_DIAMETER;
        slack = 0;
    }
    /* 向上取整，让缩放后的可见盘面铺满 D×D 裁切容器。 */
    layout.zoom = (uint16_t)(((uint32_t)layout.disc * LV_IMG_ZOOM_NONE + SERVICE_GUI_MUSIC_VINYL_DIAMETER - 1U) /
                             SERVICE_GUI_MUSIC_VINYL_DIAMETER);
    layout.slider_height = (lv_coord_t)(SERVICE_GUI_MUSIC_LAYOUT_SLIDER_HEIGHT + slack);

    layout.disc_top = layout.gap;
    layout.title_ink_top = (lv_coord_t)(layout.disc_top + layout.disc + layout.gap);
    layout.artist_ink_top = (lv_coord_t)(layout.title_ink_top + SERVICE_GUI_MUSIC_LAYOUT_TEXT_INK_H +
                                         SERVICE_GUI_MUSIC_LAYOUT_TEXT_GAP);
    layout.slider_top = (lv_coord_t)(layout.artist_ink_top + SERVICE_GUI_MUSIC_LAYOUT_TEXT_INK_H +
                                     SERVICE_GUI_MUSIC_LAYOUT_TEXT_PROGRESS);
    layout.time_ink_top = (lv_coord_t)(layout.slider_top + layout.slider_height +
                                       SERVICE_GUI_MUSIC_LAYOUT_PROGRESS_GAP);
    layout.button_top = (lv_coord_t)(layout.time_ink_top + SERVICE_GUI_MUSIC_LAYOUT_TIME_INK_H + layout.gap);
    layout.column_x = (lv_coord_t)((width - SERVICE_GUI_MUSIC_TITLE_WIDTH) / 2);
    return layout;
}

/**
 * @brief 把对象左上对齐放到父对象内容区坐标 (x, y)。
 * @param[in] obj LVGL 对象。
 * @param[in] x 相对父内容区左沿。
 * @param[in] y 相对父内容区上沿。
 */
static void service_gui_view_music_place(lv_obj_t *obj, lv_coord_t x, lv_coord_t y)
{
    lv_obj_set_align(obj, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(obj, x, y);
}

/**
 * @brief 创建圆形半透明 Wash 控制按钮及其居中符号。
 * @param[in] parent Playing 标签页。
 * @param[in] size 直径。
 * @param[in] x 左沿，相对标签页内容区。
 * @param[in] y 上沿，相对标签页内容区。
 * @param[in] symbol LVGL 符号。
 * @param[out] icon 符号 Label；可为 NULL。
 * @return 按钮对象。
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
    service_gui_view_music_place(button, x, y);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(button, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_WASH, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_transform_zoom(button, LV_IMG_ZOOM_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(button, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_WASH, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, 80, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_transform_zoom(button, LV_IMG_ZOOM_NONE, LV_PART_MAIN | LV_STATE_PRESSED);

    /* 图标按包围盒居中；对称字形的质心偏差都小于 1 px，不另移（播放三角形由 transport 右移）。 */
    label = lv_label_create(button);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_align(label, LV_ALIGN_CENTER);
    lv_label_set_text(label, symbol);
    service_gui_theme_style_add(label, SERVICE_GUI_THEME_TEXT, SERVICE_GUI_THEME_INK, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (icon != NULL)
    {
        *icon = label;
    }

    return button;
}

/**
 * @brief 创建曲名或歌手 Label：内容列宽、高为字体行高、居中单行、底透明、不可点。
 * @param[in] tab Playing 标签页。
 * @param[in] font 曲目元数据字体（GUI-24）。
 * @param[in] column_x 内容列左沿。
 * @param[in] ink_top 完整字形上沿；Label 盒子上沿 = ink_top - 字形在 Label 内的首行。
 * @return Label 对象。
 */
static lv_obj_t *service_gui_view_music_create_track_label(lv_obj_t *tab,
                                                           const lv_font_t *font,
                                                           lv_coord_t column_x,
                                                           lv_coord_t ink_top)
{
    lv_obj_t *label = lv_label_create(tab);

    lv_obj_set_size(label, SERVICE_GUI_MUSIC_TITLE_WIDTH, font->line_height);
    service_gui_view_music_place(label, column_x, (lv_coord_t)(ink_top - SERVICE_GUI_MUSIC_LAYOUT_TEXT_INK_TOP));
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(label, SERVICE_GUI_THEME_TEXT, SERVICE_GUI_THEME_INK, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    return label;
}

/**
 * @brief 创建唱盘：D×D 透明裁切容器内放原尺寸 Image，以 zoom 缩到 D。
 * @param[out] music Music 页句柄（写 vinyl_image）。
 * @param[in] tab Playing 标签页。
 * @param[in] width 标签页内容区宽度。
 * @param[in] layout 推出的版面。
 * @note  Image 保持 SERVICE_GUI_MUSIC_VINYL_DIAMETER 见方（main/vinyl 按它校验并合成），
 *        居中偏移 -(底图 - D) / 2，超出部分被容器裁掉；zoom 与旋转都绕 Image 中心，
 *        旋转脏区也被裁到 D×D。容器不可点、不可滚，按 D 参与版面。
 */
static void service_gui_view_music_create_vinyl(Service_GUI_ViewMusicTypeDef *music,
                                                lv_obj_t *tab,
                                                lv_coord_t width,
                                                const Service_GUI_ViewMusicLayoutTypeDef *layout)
{
    const lv_coord_t inset = (lv_coord_t)(((lv_coord_t)SERVICE_GUI_MUSIC_VINYL_DIAMETER - layout->disc) / 2);
    lv_obj_t *holder = lv_obj_create(tab);

    lv_obj_remove_style_all(holder);
    lv_obj_clear_flag(holder, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(holder, layout->disc, layout->disc);
    service_gui_view_music_place(holder, (lv_coord_t)((width - layout->disc) / 2), layout->disc_top);

    music->vinyl_image = lv_img_create(holder);
    lv_obj_set_size(music->vinyl_image, SERVICE_GUI_MUSIC_VINYL_DIAMETER, SERVICE_GUI_MUSIC_VINYL_DIAMETER);
    service_gui_view_music_place(music->vinyl_image, (lv_coord_t)(-inset), (lv_coord_t)(-inset));
    lv_img_set_zoom(music->vinyl_image, layout->zoom);
    lv_obj_add_flag(music->vinyl_image, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(music->vinyl_image, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief 创建进度条：Muted 轨道 + Wash 细边；Accent 进度；Knob 只在按下时显示并放大。
 * @param[in] tab Playing 标签页。
 * @param[in] layout 推出的版面。
 * @return 进度条对象。
 */
static lv_obj_t *service_gui_view_music_create_slider(lv_obj_t *tab, const Service_GUI_ViewMusicLayoutTypeDef *layout)
{
    lv_obj_t *slider = lv_slider_create(tab);

    lv_slider_set_value(slider, 0, LV_ANIM_OFF);
    lv_obj_set_size(slider, SERVICE_GUI_MUSIC_TITLE_WIDTH, layout->slider_height);
    service_gui_view_music_place(slider, layout->column_x, layout->slider_top);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(slider, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_MUTED, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, 150, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(slider, SERVICE_GUI_THEME_BORDER, SERVICE_GUI_THEME_WASH, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(slider, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(slider, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(slider, 1000, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    service_gui_theme_style_add(slider, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_ACCENT, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(slider, 1000, LV_PART_KNOB | LV_STATE_DEFAULT);
    service_gui_theme_style_add(slider, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_ACCENT, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(slider, 1000, LV_PART_KNOB | LV_STATE_PRESSED);
    service_gui_theme_style_add(slider, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_ACCENT, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(slider, 4, LV_PART_KNOB | LV_STATE_PRESSED);
    return slider;
}

/**
 * @brief 创建 Now Playing：唱盘、曲名、歌手、进度条、时间与三键，按一份像素常量配置排布（GUI-27）。
 * @param[out] music Music 页句柄。
 * @param[in] tab Now Playing 标签页。
 * @note  事件由 main/transport 绑定，曲名与歌手文本由 main/title 写入，时间文本由
 *        main/transport 写入；本处只定对象、位置、尺寸、字体与样式。各元素都是标签页的
 *        直接子对象，用内容区像素坐标左上对齐，不用百分比或 translate。
 */
static void service_gui_view_music_create_now_playing(Service_GUI_ViewMusicTypeDef *music, lv_obj_t *tab)
{
    const lv_font_t *track_font = service_gui_font_track_text();
    const lv_coord_t side_inset =
        (lv_coord_t)((SERVICE_GUI_MUSIC_LAYOUT_MAIN_BUTTON - SERVICE_GUI_MUSIC_LAYOUT_SIDE_BUTTON) / 2);
    Service_GUI_ViewMusicLayoutTypeDef layout;
    lv_coord_t width;
    lv_obj_t *label;

    service_gui_theme_style_add(tab, SERVICE_GUI_THEME_TEXT, SERVICE_GUI_THEME_INK, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_opa(tab, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);

    /* 内容区尺寸取决于 Tabview 与标签栏，先排一次版再读。 */
    lv_obj_update_layout(tab);
    width = lv_obj_get_content_width(tab);
    layout = service_gui_view_music_solve_layout(width, lv_obj_get_content_height(tab));

    service_gui_view_music_create_vinyl(music, tab, width, &layout);

    /*
     * 曲名在上、歌手在下，同为曲目元数据字体（资源包 Noto 12 px，否则 Montserrat 12，GUI-24）。
     * 曲名 Ink 不透明，长文本模式由 main/title 按宽度决定（循环滚动或静止）；歌手 Ink、
     * Opa 较低，超宽截断加省略号。位置按完整字形固定，不随文本移动。
     */
    label = service_gui_view_music_create_track_label(tab, track_font, layout.column_x, layout.title_ink_top);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_anim_speed(label, SERVICE_GUI_MUSIC_TITLE_SPEED, LV_PART_MAIN | LV_STATE_DEFAULT);
    music->title_label = label;

    label = service_gui_view_music_create_track_label(tab, track_font, layout.column_x, layout.artist_ink_top);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_opa(label, SERVICE_GUI_MUSIC_ARTIST_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);
    music->artist_label = label;

    music->slider = service_gui_view_music_create_slider(tab, &layout);

    /* 时间：宽随内容、水平居中；初始文本 `0:00/--:--` 由 main/transport prepare 按同一格式写入。 */
    label = lv_label_create(tab);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_align(label, LV_ALIGN_TOP_MID);
    lv_obj_set_pos(label, 0, (lv_coord_t)(layout.time_ink_top - SERVICE_GUI_MUSIC_LAYOUT_TIME_INK_TOP));
    service_gui_theme_style_add(label, SERVICE_GUI_THEME_TEXT, SERVICE_GUI_THEME_INK, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
    music->time_label = label;

    /* 三键共用一条水平中线；两侧键外沿对齐内容列两端，主键水平居中。 */
    music->previous_button = service_gui_view_music_create_control(
        tab, SERVICE_GUI_MUSIC_LAYOUT_SIDE_BUTTON, layout.column_x,
        (lv_coord_t)(layout.button_top + side_inset), LV_SYMBOL_PREV, NULL);
    music->play_pause_button = service_gui_view_music_create_control(
        tab, SERVICE_GUI_MUSIC_LAYOUT_MAIN_BUTTON,
        (lv_coord_t)((width - SERVICE_GUI_MUSIC_LAYOUT_MAIN_BUTTON) / 2), layout.button_top,
        LV_SYMBOL_PLAY, &music->play_pause_icon);
    lv_obj_set_style_text_font(music->play_pause_icon, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_x(music->play_pause_icon, SERVICE_GUI_MUSIC_PLAY_ICON_SHIFT_X);
    music->next_button = service_gui_view_music_create_control(
        tab, SERVICE_GUI_MUSIC_LAYOUT_SIDE_BUTTON,
        (lv_coord_t)(layout.column_x + SERVICE_GUI_MUSIC_TITLE_WIDTH - SERVICE_GUI_MUSIC_LAYOUT_SIDE_BUTTON),
        (lv_coord_t)(layout.button_top + side_inset), LV_SYMBOL_NEXT, NULL);
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
    service_gui_theme_style_add(tabs, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_WASH, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_view_music_style_tab_buttons(lv_tabview_get_tab_btns(tabs));
    service_gui_view_music_style_tab_content(tabs);

    service_gui_view_music_create_now_playing(music, service_gui_view_music_add_tab(tabs, "Playing"));
    music->queue_tab = service_gui_view_music_add_tab(tabs, "Queue");
    service_gui_view_music_style_queue(music->queue_tab);
    (void)service_gui_view_music_add_tab(tabs, "Library");

    music->tabs = tabs;
}
