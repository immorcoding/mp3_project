/**
  ******************************************************************************
  * @file    track_title_prototype.c
  * @brief   一次性唱盘页曲名原型，不进入产品固件。
  *
  * @details 第三轮按用户反馈固定 12 px 与单行循环滚动，重排唱盘、曲名、时间
  *          和进度的纵向节奏：G 上置居中、H 下置曲名/时间分栏、I 上置分栏；
  *          J 按用户补充改为唱盘→曲名→进度→时间→按钮。唱盘始终为原生
  *          144 px，不使用悬浮底板。方向键切换方案与状态，
  *          数字键 1..7 直达状态。本文件只随 host 模拟器进入临时原型分支，
  *          不是产品 Screen 的第二事实源；产品 view/ 与运行时实现不改。
  ******************************************************************************
  */

#include "track_title_prototype.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "Service/gui/theme/gui_service_theme_style.h"
#include "Service/gui/view/gui_service_view.h"

LV_FONT_DECLARE(lv_font_track_title_12);

#define TRACK_TITLE_START_MS           6000U
#define TRACK_TITLE_VARIANT_COUNT      4U
#define TRACK_TITLE_STATE_COUNT        7U
#define TRACK_TITLE_TOOLBAR_BYTES      96U
#define TRACK_TITLE_VINYL_NATIVE_SIZE  144U
#define TRACK_TITLE_VINYL_ZOOM_NONE    256U

typedef struct
{
    char key;                /**< 悬浮条与日志中的方案键。 */
    const char *name;        /**< 方案短名。 */
} TrackTitlePrototypeVariantTypeDef;

typedef struct
{
    const char *name;      /**< 悬浮条与日志中的状态短名。 */
    const char *title;     /**< 演示曲目显示名或防御性占位。 */
    const char *status;    /**< 时间/状态行的演示文本。 */
    uint8_t progress;      /**< 现有进度条的演示百分比。 */
} TrackTitlePrototypeStateTypeDef;

typedef struct
{
    lv_obj_t *title;          /**< 曲名 Label。 */
    lv_obj_t *time_home;      /**< 时间 Label 原有的控制区父对象。 */
    lv_obj_t *toolbar;        /**< 原型切换条。 */
    lv_obj_t *toolbar_label;  /**< 当前方案与状态文本。 */
    uint8_t variant;          /**< 当前方案下标。 */
    uint8_t state;            /**< 当前演示状态下标。 */
    bool entered;             /**< 是否已越过启动序列并进入 Main。 */
    bool dirty;               /**< 是否需重排曲名区域。 */
} TrackTitlePrototypeTypeDef;

static const TrackTitlePrototypeVariantTypeDef track_title_variants[TRACK_TITLE_VARIANT_COUNT] = {
    {'G', "Center"},
    {'H', "Below split"},
    {'I', "Above split"},
    {'J', "Stacked"},
};

static const TrackTitlePrototypeStateTypeDef track_title_states[TRACK_TITLE_STATE_COUNT] = {
    {"Short", "晴天", "0:42/3:12", 22U},
    {"Long", "01 - 不能说的秘密 / 蒲公英的约定 / 回到过去", "0:42/3:12", 22U},
    {"Mixed", "Jay Chou 周杰伦 - 最长的电影（Live in Hong Kong）", "0:42/3:12", 22U},
    {"No card", "--", "No SD card", 0U},
    {"Empty", "--", "No tracks", 0U},
    {"Bad skip", "River Flows in You", "Skipped 1 file", 0U},
    {"Next track", "Merry Christmas Mr. Lawrence", "0:00/--:--", 0U},
};

static TrackTitlePrototypeTypeDef track_title_prototype;

/**
 * @brief 测量一段文本在 12 px 原型字体下的宽度。
 * @param[in] text 以 NUL 结尾的 UTF-8 文本。
 * @return 文本宽度，单位为像素。
 */
static lv_coord_t track_title_measure(const char *text)
{
    return lv_txt_get_width(text, (uint32_t)strlen(text), &lv_font_track_title_12, 0, LV_TEXT_FLAG_NONE);
}

/**
 * @brief 更新悬浮条并打印当前完整演示状态。
 */
static void track_title_prototype_report(void)
{
    const TrackTitlePrototypeVariantTypeDef *variant =
        &track_title_variants[track_title_prototype.variant];
    const TrackTitlePrototypeStateTypeDef *state =
        &track_title_states[track_title_prototype.state];
    char text[TRACK_TITLE_TOOLBAR_BYTES];

    (void)snprintf(text, sizeof(text), "%c %s | %s", variant->key, variant->name, state->name);
    if (track_title_prototype.toolbar_label != NULL)
    {
        lv_label_set_text(track_title_prototype.toolbar_label, text);
    }
    printf("[track-title-prototype] variant=%c (%s), font=12 px, state=%s, title=%s, status=%s\n",
           variant->key,
           variant->name,
           state->name,
           state->title,
           state->status);
}

/**
 * @brief 切换到指定方案并使布局失效。
 * @param[in] index 方案下标；调用方保证小于方案数。
 */
static void track_title_prototype_set_variant(uint8_t index)
{
    track_title_prototype.variant = index;
    track_title_prototype.dirty = true;
    track_title_prototype_report();
}

/**
 * @brief 切换到指定演示状态并使曲名失效。
 * @param[in] index 状态下标；调用方保证小于状态数。
 */
static void track_title_prototype_set_state(uint8_t index)
{
    track_title_prototype.state = index;
    track_title_prototype.dirty = true;
    track_title_prototype_report();
}

/**
 * @brief 悬浮条左按钮回调。
 * @param[in] event LVGL 点击事件。
 */
static void track_title_prototype_on_previous_variant(lv_event_t *event)
{
    (void)event;
    track_title_prototype_previous_variant();
}

/**
 * @brief 悬浮条右按钮回调。
 * @param[in] event LVGL 点击事件。
 */
static void track_title_prototype_on_next_variant(lv_event_t *event)
{
    (void)event;
    track_title_prototype_next_variant();
}

/**
 * @brief 悬浮条中间按钮回调，循环切换演示状态。
 * @param[in] event LVGL 点击事件。
 */
static void track_title_prototype_on_next_state(lv_event_t *event)
{
    (void)event;
    track_title_prototype_next_state();
}

/**
 * @brief 创建一个原型悬浮条按钮。
 * @param[in] parent 悬浮条容器。
 * @param[in] text 按钮文本。
 * @param[in] width 按钮宽度。
 * @param[in] callback 点击回调。
 * @param[out] label 可选的按钮 Label 句柄。
 * @return 按钮对象。
 */
static lv_obj_t *track_title_prototype_create_button(lv_obj_t *parent,
                                                     const char *text,
                                                     lv_coord_t width,
                                                     lv_event_cb_t callback,
                                                     lv_obj_t **label)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_t *button_label = lv_label_create(button);

    lv_obj_set_size(button, width, 22);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(button, 11, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(button, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_WASH,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(button, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_ACCENT,
                                LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, 50, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    lv_label_set_text(button_label, text);
    lv_obj_set_align(button_label, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(button_label, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(button_label, SERVICE_GUI_THEME_TEXT, SERVICE_GUI_THEME_INK,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    if (label != NULL)
    {
        *label = button_label;
    }
    return button;
}

/**
 * @brief 创建底部悬浮切换条；它只用于原型，不属于候选界面。
 */
static void track_title_prototype_create_toolbar(void)
{
    lv_obj_t *bar = lv_obj_create(lv_layer_top());

    lv_obj_set_size(bar, 232, 28);
    lv_obj_set_align(bar, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_pos(bar, 0, -2);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(bar, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bar, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(bar, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_GROUND,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, 225, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(bar, SERVICE_GUI_THEME_BORDER, SERVICE_GUI_THEME_INK,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bar, 70, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN | LV_STATE_DEFAULT);

    (void)track_title_prototype_create_button(bar, "<", 24,
                                              track_title_prototype_on_previous_variant, NULL);
    (void)track_title_prototype_create_button(bar, "", 166,
                                              track_title_prototype_on_next_state,
                                              &track_title_prototype.toolbar_label);
    (void)track_title_prototype_create_button(bar, ">", 24,
                                              track_title_prototype_on_next_variant, NULL);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    track_title_prototype.toolbar = bar;
}

/**
 * @brief 按当前方案重排原尺寸唱盘、12 px 曲名与时间/状态行。
 */
static void track_title_prototype_apply(void)
{
    const Service_GUI_ViewTypeDef *view = service_gui_view_get();
    const TrackTitlePrototypeStateTypeDef *state =
        &track_title_states[track_title_prototype.state];
    lv_obj_t *title = track_title_prototype.title;
    lv_obj_t *vinyl = view->music.vinyl_image;
    lv_obj_t *playing_tab = lv_obj_get_parent(vinyl);
    lv_obj_t *time_label = view->music.time_label;
    lv_coord_t title_width;

    lv_obj_set_style_text_font(title, &lv_font_track_title_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(title, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_align(title, LV_ALIGN_TOP_LEFT);
    lv_obj_set_size(vinyl, TRACK_TITLE_VINYL_NATIVE_SIZE, TRACK_TITLE_VINYL_NATIVE_SIZE);
    lv_img_set_zoom(vinyl, TRACK_TITLE_VINYL_ZOOM_NONE);
    lv_obj_set_align(vinyl, LV_ALIGN_TOP_MID);

    if (lv_obj_get_parent(time_label) != track_title_prototype.time_home)
    {
        lv_obj_set_parent(time_label, track_title_prototype.time_home);
    }
    lv_obj_clear_flag(time_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_long_mode(time_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(time_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_align(time_label, LV_ALIGN_TOP_MID);
    lv_obj_set_pos(time_label, 0, lv_pct(5));
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(view->music.slider, lv_pct(0), lv_pct(20));
    lv_obj_set_pos(view->music.previous_button, lv_pct(-30), lv_pct(19));
    lv_obj_set_pos(view->music.next_button, lv_pct(30), lv_pct(19));
    lv_obj_set_pos(view->music.play_pause_button, lv_pct(0), lv_pct(18));

    switch (track_title_prototype.variant)
    {
    case 0U:
        title_width = 218;
        lv_obj_set_pos(title, 8, 4);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(vinyl, 0, 24); /* 标签→曲名→唱盘→原时间行形成均匀间隔。 */
        break;

    case 1U:
        title_width = 126;
        lv_obj_set_pos(title, 12, 158);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(vinyl, 0, 6); /* 唱盘→曲名/时间分栏→进度保持清晰间隔。 */
        if (lv_obj_get_parent(time_label) != playing_tab)
        {
            lv_obj_set_parent(time_label, playing_tab);
        }
        lv_obj_set_align(time_label, LV_ALIGN_TOP_RIGHT);
        lv_label_set_long_mode(time_label, LV_LABEL_LONG_DOT);
        lv_obj_set_size(time_label, 80, lv_font_montserrat_10.line_height + 2);
        lv_obj_set_pos(time_label, -12, 158);
        lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
        break;

    case 2U:
        title_width = 126;
        lv_obj_set_pos(title, 12, 4);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(vinyl, 0, 26); /* 曲名/时间在上方分栏，唱盘下方只留进度。 */
        if (lv_obj_get_parent(time_label) != playing_tab)
        {
            lv_obj_set_parent(time_label, playing_tab);
        }
        lv_obj_set_align(time_label, LV_ALIGN_TOP_RIGHT);
        lv_label_set_long_mode(time_label, LV_LABEL_LONG_DOT);
        lv_obj_set_size(time_label, 80, lv_font_montserrat_10.line_height + 2);
        lv_obj_set_pos(time_label, -12, 4);
        lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
        break;

    default:
        title_width = 218;
        lv_obj_set_pos(title, 8, 152);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(vinyl, 0, 4); /* 唱盘→曲名→进度→时间→按钮，曲名不再分栏。 */
        lv_obj_set_pos(view->music.slider, lv_pct(0), 10);
        lv_obj_set_pos(time_label, 0, 18);
        lv_obj_set_y(view->music.previous_button, lv_obj_get_y_aligned(view->music.previous_button) + 4);
        lv_obj_set_y(view->music.next_button, lv_obj_get_y_aligned(view->music.next_button) + 4);
        lv_obj_set_y(view->music.play_pause_button, lv_obj_get_y_aligned(view->music.play_pause_button) + 4);
        break;
    }

    lv_label_set_long_mode(title,
                           (track_title_measure(state->title) > title_width) ?
                               LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
    lv_obj_set_size(title, title_width, lv_font_track_title_12.line_height + 2);
    lv_label_set_text(title, state->title);
    lv_obj_move_foreground(title);
    lv_obj_move_foreground(time_label);
    track_title_prototype.dirty = false;
}

/**
 * @brief 创建曲名与原型切换条，记录时间 Label 原父对象；初始为 G 上置居中短曲名。
 * @note 仅在 Service_GUI_Init() 与 gui_music_init() 成功后、进入模拟主循环前调用一次；
 *       必须早于 track_title_prototype_step()。普通模拟器模式不得调用。
 */
void track_title_prototype_init(void)
{
    lv_obj_t *playing_tab = lv_obj_get_parent(service_gui_view_get()->music.vinyl_image);

    (void)memset(&track_title_prototype, 0, sizeof(track_title_prototype));
    track_title_prototype.title = lv_label_create(playing_tab);
    lv_obj_clear_flag(track_title_prototype.title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_opa(track_title_prototype.title, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_anim_speed(track_title_prototype.title, 24, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(track_title_prototype.title,
                                SERVICE_GUI_THEME_TEXT,
                                SERVICE_GUI_THEME_INK,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(track_title_prototype.title, LV_OBJ_FLAG_HIDDEN);

    track_title_prototype.time_home =
        lv_obj_get_parent(service_gui_view_get()->music.time_label);
    track_title_prototype.state = 0U;
    track_title_prototype.dirty = true;
    track_title_prototype_create_toolbar();
    track_title_prototype_report();
}

/**
 * @brief 在启动序列后进入真实唱盘页，并覆盖演示专用的曲名、时间与进度。
 * @param[in] elapsed_ms 自 GUI Service 初始化完成起的模拟毫秒数。
 * @note 每圈在 gui_music_step() 之后、Service_GUI_Process() 之前调用，确保演示文本
 *       先于本帧绘制覆盖假后端默认值；须先完成 track_title_prototype_init()。
 */
void track_title_prototype_step(uint32_t elapsed_ms)
{
    const Service_GUI_ViewTypeDef *view = service_gui_view_get();
    const TrackTitlePrototypeStateTypeDef *state;

    if (elapsed_ms < TRACK_TITLE_START_MS)
    {
        return;
    }

    if (lv_scr_act() != view->main.screen)
    {
        lv_scr_load(view->main.screen);
    }
    if (lv_tabview_get_tab_act(view->music.tabs) != 0U)
    {
        lv_tabview_set_act(view->music.tabs, 0U, LV_ANIM_OFF);
    }

    if (!track_title_prototype.entered)
    {
        track_title_prototype.entered = true;
        lv_obj_clear_flag(track_title_prototype.title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(track_title_prototype.toolbar, LV_OBJ_FLAG_HIDDEN);
        track_title_prototype.dirty = true;
    }
    if (track_title_prototype.dirty)
    {
        track_title_prototype_apply();
    }

    state = &track_title_states[track_title_prototype.state];
    lv_label_set_text(view->music.time_label, state->status);
    lv_slider_set_value(view->music.slider, state->progress, LV_ANIM_OFF);
}

/**
 * @brief 处理 ASCII 演示键。
 * @param[in] key ASCII 键值；V 切方案、N 切状态、1..7 直达状态。
 * @return 已消费该键为 true，否则 false。
 */
bool track_title_prototype_handle_key(char key)
{
    if ((key == 'v') || (key == 'V'))
    {
        track_title_prototype_next_variant();
        return true;
    }
    if ((key == 'n') || (key == 'N'))
    {
        track_title_prototype_next_state();
        return true;
    }
    if ((key >= '1') && (key <= '7'))
    {
        track_title_prototype_set_state((uint8_t)(key - '1'));
        return true;
    }
    return false;
}

/**
 * @brief 切换到上一个方案，到开头时循环到末尾。
 */
void track_title_prototype_previous_variant(void)
{
    const uint8_t index = (track_title_prototype.variant == 0U) ?
                          (TRACK_TITLE_VARIANT_COUNT - 1U) :
                          (track_title_prototype.variant - 1U);
    track_title_prototype_set_variant(index);
}

/**
 * @brief 切换到下一个方案，到末尾时循环到开头。
 */
void track_title_prototype_next_variant(void)
{
    track_title_prototype_set_variant((track_title_prototype.variant + 1U) % TRACK_TITLE_VARIANT_COUNT);
}

/**
 * @brief 切换到上一个演示状态，到开头时循环到末尾。
 */
void track_title_prototype_previous_state(void)
{
    const uint8_t index = (track_title_prototype.state == 0U) ?
                          (TRACK_TITLE_STATE_COUNT - 1U) :
                          (track_title_prototype.state - 1U);
    track_title_prototype_set_state(index);
}

/**
 * @brief 切换到下一个演示状态，到末尾时循环到开头。
 */
void track_title_prototype_next_state(void)
{
    track_title_prototype_set_state((track_title_prototype.state + 1U) % TRACK_TITLE_STATE_COUNT);
}
