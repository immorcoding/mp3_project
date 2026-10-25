/**
  ******************************************************************************
  * @file    track_title_prototype.c
  * @brief   一次性唱盘页曲名原型，不进入产品固件。
  *
  * @details 在真实 Now Playing 页上比较三种结构：A 上置 16 px 单行滚动、
  *          B 下置 18 px 双行省略、C 右侧 14 px 三行省略。方向键切换方案与
  *          状态，底部悬浮条提供同样的鼠标操作。所有状态均为内存中的静态演示。
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

LV_FONT_DECLARE(lv_font_track_title_14);
LV_FONT_DECLARE(lv_font_track_title_16);
LV_FONT_DECLARE(lv_font_track_title_18);

#define TRACK_TITLE_START_MS           6000U
#define TRACK_TITLE_VARIANT_COUNT      3U
#define TRACK_TITLE_STATE_COUNT        7U
#define TRACK_TITLE_FORMATTED_BYTES    256U
#define TRACK_TITLE_TOOLBAR_BYTES      96U
#define TRACK_TITLE_VINYL_NATIVE_SIZE  144U
#define TRACK_TITLE_VINYL_ZOOM_NONE    256U

typedef struct
{
    char key;                /**< 悬浮条与日志中的方案键。 */
    const char *name;        /**< 方案短名。 */
    const lv_font_t *font;   /**< 本方案候选字号。 */
    uint8_t max_lines;       /**< 省略方案的最大行数；滚动方案为 1。 */
    bool scroll;             /**< 是否使用单行循环滚动。 */
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
    lv_obj_t *toolbar_label;  /**< 当前方案与状态文本。 */
    uint8_t variant;          /**< 当前方案下标。 */
    uint8_t state;            /**< 当前演示状态下标。 */
    bool entered;             /**< 是否已越过启动序列并进入 Main。 */
    bool dirty;               /**< 是否需重排曲名区域。 */
} TrackTitlePrototypeTypeDef;

static const TrackTitlePrototypeVariantTypeDef track_title_variants[TRACK_TITLE_VARIANT_COUNT] = {
    {'A', "Top scroll 16", &lv_font_track_title_16, 1U, true},
    {'B', "Bottom 2-line 18", &lv_font_track_title_18, 2U, false},
    {'C', "Side 3-line 14", &lv_font_track_title_14, 3U, false},
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
 * @brief 返回 UTF-8 文本中下一个字符的字节下标。
 * @param[in] text 以 NUL 结尾的有效 UTF-8 文本。
 * @param[in] pos 当前字符的字节下标。
 * @return 下一个字符的字节下标；演示文本不含非法序列。
 */
static size_t track_title_utf8_next(const char *text, size_t pos)
{
    const uint8_t lead = (uint8_t)text[pos];
    size_t length = 1U;

    if ((lead & 0xE0U) == 0xC0U)
    {
        length = 2U;
    }
    else if ((lead & 0xF0U) == 0xE0U)
    {
        length = 3U;
    }
    else if ((lead & 0xF8U) == 0xF0U)
    {
        length = 4U;
    }

    return pos + length;
}

/**
 * @brief 测量一个字节前缀在给定字体下的宽度。
 * @param[in] text 文本起点，不要求在前缀末尾有 NUL。
 * @param[in] length 参与测量的字节数。
 * @param[in] font LVGL 字体。
 * @return 文本宽度，单位为像素。
 */
static lv_coord_t track_title_measure(const char *text, size_t length, const lv_font_t *font)
{
    return lv_txt_get_width(text, (uint32_t)length, font, 0, LV_TEXT_FLAG_NONE);
}

/**
 * @brief 判断一个字节是否为演示换行所用的 ASCII 字母或数字。
 * @param[in] value 待判断字节。
 * @return 是 ASCII 字母或数字为 true。
 */
static bool track_title_ascii_alnum(char value)
{
    const uint8_t byte = (uint8_t)value;

    return ((byte >= (uint8_t)'0') && (byte <= (uint8_t)'9')) ||
           ((byte >= (uint8_t)'A') && (byte <= (uint8_t)'Z')) ||
           ((byte >= (uint8_t)'a') && (byte <= (uint8_t)'z'));
}

/**
 * @brief 非末行若在 ASCII 单词中间断开，把整个单词留给下一行。
 * @param[in] text 当前行文本起点。
 * @param[in] length 当前剩余文本字节数。
 * @param[in] prefix 按像素找到的断行字节数。
 * @return 调整后的断行字节数；行首就是超长单词时保留原断点。
 */
static size_t track_title_avoid_ascii_word_split(const char *text, size_t length, size_t prefix)
{
    size_t word_start;
    size_t adjusted;

    if ((prefix == 0U) || (prefix >= length) ||
        !track_title_ascii_alnum(text[prefix - 1U]) ||
        !track_title_ascii_alnum(text[prefix]))
    {
        return prefix;
    }

    word_start = prefix;
    while ((word_start > 0U) && track_title_ascii_alnum(text[word_start - 1U]))
    {
        word_start--;
    }
    if (word_start == 0U)
    {
        return prefix;
    }

    adjusted = word_start;
    if ((adjusted > 0U) && (text[adjusted - 1U] == '('))
    {
        adjusted--;
    }
    else if ((adjusted >= 3U) &&
             ((uint8_t)text[adjusted - 3U] == 0xEFU) &&
             ((uint8_t)text[adjusted - 2U] == 0xBCU) &&
             ((uint8_t)text[adjusted - 1U] == 0x88U))
    {
        adjusted -= 3U;
    }

    return (adjusted == 0U) ? prefix : adjusted;
}

/**
 * @brief 找到可容纳指定后缀的最长 UTF-8 前缀。
 * @param[in] text 文本起点。
 * @param[in] length 可选择的文本字节数。
 * @param[in] font LVGL 字体。
 * @param[in] width 可用像素宽度。
 * @param[in] reserved_width 为省略号等后缀预留的像素宽度。
 * @return 最长可容纳前缀的字节数；首字符本身过宽时仍返回首字符长度。
 */
static size_t track_title_fit_prefix(const char *text,
                                     size_t length,
                                     const lv_font_t *font,
                                     lv_coord_t width,
                                     lv_coord_t reserved_width)
{
    size_t pos = 0U;
    size_t best = 0U;

    while (pos < length)
    {
        const size_t next = track_title_utf8_next(text, pos);

        if ((next > length) ||
            ((track_title_measure(text, next, font) + reserved_width) > width))
        {
            break;
        }
        best = next;
        pos = next;
    }

    if ((best == 0U) && (length > 0U))
    {
        best = track_title_utf8_next(text, 0U);
        if (best > length)
        {
            best = length;
        }
    }
    return best;
}

/**
 * @brief 向演示格式化缓冲追加字节并保持 NUL 结尾。
 * @param[out] output 输出缓冲。
 * @param[in] output_size 输出缓冲总字节数。
 * @param[in,out] used 已使用字节数，不含 NUL。
 * @param[in] text 待追加文本。
 * @param[in] length 待追加字节数。
 */
static void track_title_append(char *output,
                               size_t output_size,
                               size_t *used,
                               const char *text,
                               size_t length)
{
    if ((*used >= (output_size - 1U)) || (length == 0U))
    {
        return;
    }
    if (length > (output_size - 1U - *used))
    {
        length = output_size - 1U - *used;
    }
    (void)memcpy(&output[*used], text, length);
    *used += length;
    output[*used] = '\0';
}

/**
 * @brief 把文本按字符边界排成固定行数，末行装不下时用省略号收尾。
 * @param[in] text 原始 UTF-8 文本。
 * @param[in] font LVGL 字体。
 * @param[in] width 每行可用像素宽度。
 * @param[in] max_lines 最大行数。
 * @param[out] output 格式化结果缓冲。
 * @param[in] output_size 输出缓冲总字节数。
 */
static void track_title_format_lines(const char *text,
                                     const lv_font_t *font,
                                     lv_coord_t width,
                                     uint8_t max_lines,
                                     char *output,
                                     size_t output_size)
{
    static const char ellipsis[] = "…";
    const lv_coord_t ellipsis_width = track_title_measure(ellipsis, strlen(ellipsis), font);
    const char *remaining = text;
    size_t used = 0U;
    uint8_t line;

    output[0] = '\0';
    for (line = 0U; line < max_lines; line++)
    {
        const size_t length = strlen(remaining);
        const bool last_line = (line + 1U == max_lines);
        size_t prefix;

        if (track_title_measure(remaining, length, font) <= width)
        {
            track_title_append(output, output_size, &used, remaining, length);
            return;
        }

        prefix = track_title_fit_prefix(remaining,
                                        length,
                                        font,
                                        width,
                                        last_line ? ellipsis_width : 0);
        if (!last_line)
        {
            prefix = track_title_avoid_ascii_word_split(remaining, length, prefix);
        }
        track_title_append(output, output_size, &used, remaining, prefix);
        if (last_line)
        {
            track_title_append(output, output_size, &used, ellipsis, strlen(ellipsis));
            return;
        }

        track_title_append(output, output_size, &used, "\n", 1U);
        remaining += prefix;
    }
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
    printf("[track-title-prototype] variant=%c (%s), state=%s, title=%s, status=%s\n",
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
    lv_obj_set_style_bg_color(button, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, 50, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    lv_label_set_text(button_label, text);
    lv_obj_set_align(button_label, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(button_label, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(button_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
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
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x101018), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, 225, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bar, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
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
 * @brief 按当前方案重排唱盘与曲名，并应用当前状态文本。
 */
static void track_title_prototype_apply(void)
{
    const Service_GUI_ViewTypeDef *view = service_gui_view_get();
    const TrackTitlePrototypeVariantTypeDef *variant =
        &track_title_variants[track_title_prototype.variant];
    const TrackTitlePrototypeStateTypeDef *state =
        &track_title_states[track_title_prototype.state];
    lv_obj_t *title = track_title_prototype.title;
    lv_obj_t *vinyl = view->music.vinyl_image;
    const char *display_text = state->title;
    char formatted[TRACK_TITLE_FORMATTED_BYTES];
    lv_coord_t title_width;
    lv_coord_t title_height;

    lv_obj_set_style_text_font(title, variant->font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_align(title, LV_ALIGN_TOP_LEFT);
    lv_obj_set_size(vinyl, TRACK_TITLE_VINYL_NATIVE_SIZE, TRACK_TITLE_VINYL_NATIVE_SIZE);

    switch (track_title_prototype.variant)
    {
    case 0U:
        title_width = 218;
        title_height = variant->font->line_height + 4;
        lv_obj_set_pos(title, 8, 0);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_img_set_zoom(vinyl, 199); /* 可见直径 112 px。 */
        lv_obj_set_align(vinyl, LV_ALIGN_TOP_MID);
        lv_obj_set_pos(vinyl, 0, 12); /* 图像可见顶部 28 px。 */
        break;

    case 1U:
        title_width = 218;
        title_height = (variant->font->line_height * 2) + 6;
        lv_obj_set_pos(title, 8, 108);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_img_set_zoom(vinyl, 171); /* 可见直径 96 px。 */
        lv_obj_set_align(vinyl, LV_ALIGN_TOP_MID);
        lv_obj_set_pos(vinyl, 0, -20); /* 图像可见顶部 4 px。 */
        break;

    default:
        title_width = 114;
        title_height = (variant->font->line_height * 3) + 6;
        lv_obj_set_pos(title, 112, 28);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_img_set_zoom(vinyl, 185); /* 可见直径 104 px。 */
        lv_obj_set_align(vinyl, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(vinyl, -17, -4); /* 图像可见区域约 x=3..107、y=16..120。 */
        break;
    }

    if (variant->scroll)
    {
        const lv_coord_t text_width = track_title_measure(state->title, strlen(state->title), variant->font);
        lv_label_set_long_mode(title,
                               (text_width > title_width) ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
    }
    else
    {
        track_title_format_lines(state->title,
                                 variant->font,
                                 title_width,
                                 variant->max_lines,
                                 formatted,
                                 sizeof(formatted));
        display_text = formatted;
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    }

    lv_obj_set_size(title, title_width, title_height);
    lv_label_set_text(title, display_text);
    lv_obj_move_foreground(title);
    track_title_prototype.dirty = false;
}

/**
 * @brief 创建曲名 Label 与原型切换条；初始状态为短曲名。
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
    track_title_prototype.state = 0U;
    track_title_prototype.dirty = true;
    track_title_prototype_create_toolbar();
    track_title_prototype_report();
}

/**
 * @brief 在启动序列后进入真实唱盘页，并覆盖演示专用的曲名、时间与进度。
 * @param[in] elapsed_ms 自 GUI Service 初始化完成起的模拟毫秒数。
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
 * @param[in] key ASCII 键值，大小写等价；V 切方案，N 切状态。
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
