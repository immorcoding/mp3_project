/**
  ******************************************************************************
  * @file    track_title_prototype.c
  * @brief   一次性唱盘页曲名原型，不进入产品固件。
  *
  * @details 第二轮根据用户反馈保留原生 144 px 唱盘，以 10/12 px 小字号比较
  *          D 上置、E 下置、F 唱盘下沿悬浮三种结构；长曲名均为单行循环滚动。
  *          方向键切换方案与状态，P 切换字号，数字键 1..7 直达状态。
  *          本文件只随 host 模拟器进入临时原型分支，不是产品 Screen 的第二事实源；
  *          产品 view/ 与运行时实现不改，定稿后只把决定交给后续 spec。
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

LV_FONT_DECLARE(lv_font_track_title_10);
LV_FONT_DECLARE(lv_font_track_title_12);

#define TRACK_TITLE_START_MS           6000U
#define TRACK_TITLE_VARIANT_COUNT      3U
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
    const char *status;    /**< 现有时间/状态行的演示文本。 */
    uint8_t progress;      /**< 现有进度条的演示百分比。 */
} TrackTitlePrototypeStateTypeDef;

typedef struct
{
    lv_obj_t *title;          /**< 曲名 Label。 */
    lv_obj_t *toolbar;        /**< 原型切换条。 */
    lv_obj_t *toolbar_label;  /**< 当前方案、字号与状态文本。 */
    uint8_t variant;          /**< 当前方案下标。 */
    uint8_t state;            /**< 当前演示状态下标。 */
    bool compact_font;        /**< false = 12 px；true = 10 px。 */
    bool entered;             /**< 是否已越过启动序列并进入 Main。 */
    bool dirty;               /**< 是否需重排曲名区域。 */
} TrackTitlePrototypeTypeDef;

static const TrackTitlePrototypeVariantTypeDef track_title_variants[TRACK_TITLE_VARIANT_COUNT] = {
    {'D', "Above"},
    {'E', "Below"},
    {'F', "Overlay"},
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
 * @brief 测量一段文本在当前原型字体下的宽度。
 * @param[in] text 以 NUL 结尾的 UTF-8 文本。
 * @param[in] font LVGL 字体。
 * @return 文本宽度，单位为像素。
 */
static lv_coord_t track_title_measure(const char *text, const lv_font_t *font)
{
    return lv_txt_get_width(text, (uint32_t)strlen(text), font, 0, LV_TEXT_FLAG_NONE);
}

/**
 * @brief 取得当前选定的原型字号。
 * @return 10 px 或 12 px 的演示 CJK 字体。
 */
static const lv_font_t *track_title_prototype_font(void)
{
    return track_title_prototype.compact_font ?
           &lv_font_track_title_10 : &lv_font_track_title_12;
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
    const unsigned font_size = track_title_prototype.compact_font ? 10U : 12U;
    char text[TRACK_TITLE_TOOLBAR_BYTES];

    (void)snprintf(text, sizeof(text), "%c %s %upx | %s",
                   variant->key, variant->name, font_size, state->name);
    if (track_title_prototype.toolbar_label != NULL)
    {
        lv_label_set_text(track_title_prototype.toolbar_label, text);
    }
    printf("[track-title-prototype] variant=%c (%s), font=%u px, state=%s, title=%s, status=%s\n",
           variant->key,
           variant->name,
           font_size,
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
 * @brief 在 10 px 与 12 px 之间切换，并让当前方案按新字高重排。
 */
static void track_title_prototype_toggle_font(void)
{
    track_title_prototype.compact_font = !track_title_prototype.compact_font;
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
 * @brief 按当前方案与字号重排原尺寸唱盘和单行曲名，并应用当前状态文本。
 */
static void track_title_prototype_apply(void)
{
    const Service_GUI_ViewTypeDef *view = service_gui_view_get();
    const TrackTitlePrototypeStateTypeDef *state =
        &track_title_states[track_title_prototype.state];
    const lv_font_t *font = track_title_prototype_font();
    lv_obj_t *title = track_title_prototype.title;
    lv_obj_t *vinyl = view->music.vinyl_image;
    lv_coord_t title_width;
    lv_coord_t title_height;
    lv_coord_t text_width;

    text_width = track_title_measure(state->title, font);
    lv_obj_set_style_text_font(title, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(title, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_align(title, LV_ALIGN_TOP_LEFT);
    lv_obj_set_size(vinyl, TRACK_TITLE_VINYL_NATIVE_SIZE, TRACK_TITLE_VINYL_NATIVE_SIZE);
    lv_img_set_zoom(vinyl, TRACK_TITLE_VINYL_ZOOM_NONE);
    lv_obj_set_align(vinyl, LV_ALIGN_TOP_MID);

    switch (track_title_prototype.variant)
    {
    case 0U:
        title_width = 218;
        title_height = font->line_height + 2;
        lv_obj_set_pos(title, 8, 0);
        lv_obj_set_pos(vinyl, 0, 17); /* 原盘 144 px；仅比原布局下移 2 px。 */
        break;

    case 1U:
        title_width = 218;
        title_height = font->line_height + 2;
        lv_obj_set_pos(title, 8, 151);
        lv_obj_set_pos(vinyl, 0, 5); /* 原盘 144 px；上移 10 px 换取下方曲名留白。 */
        break;

    default:
        title_width = (text_width + 16 < 170) ? (text_width + 16) : 170;
        title_height = font->line_height + 6;
        lv_obj_set_pos(title, (lv_obj_get_width(lv_obj_get_parent(vinyl)) - title_width) / 2, 136);
        lv_obj_set_pos(vinyl, 0, 15); /* 原盘尺寸与位置均不变，曲名悬浮于下沿。 */
        lv_obj_set_style_bg_opa(title, 185, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(title, 9, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_hor(title, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
        break;
    }

    lv_label_set_long_mode(title,
                           (text_width > (title_width - ((track_title_prototype.variant == 2U) ? 8 : 0))) ?
                               LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
    lv_obj_set_size(title, title_width, title_height);
    lv_label_set_text(title, state->title);
    lv_obj_move_foreground(title);
    track_title_prototype.dirty = false;
}

/**
 * @brief 创建曲名 Label 与原型切换条；初始为 12 px 短曲名。
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
    service_gui_theme_style_add(track_title_prototype.title,
                                SERVICE_GUI_THEME_BG,
                                SERVICE_GUI_THEME_GROUND,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(track_title_prototype.title, LV_OBJ_FLAG_HIDDEN);
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
 * @param[in] key ASCII 键值；V 切方案、N 切状态、P 切字号、1..7 直达状态。
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
    if ((key == 'p') || (key == 'P'))
    {
        track_title_prototype_toggle_font();
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
