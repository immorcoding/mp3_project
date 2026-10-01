/**
  ******************************************************************************
  * @file    gui_service_view_lock.c
  * @brief   Lock Screen 的对象树、静态样式、呼吸提示与上滑解锁。
  *
  * @details
  *          锁屏首版是待机视觉页：大号时间、日期、电量与底部解锁提示。
  *          上滑手势在本 Screen 内淡出到 Main；不涉及安全认证。
  ******************************************************************************
  */

#include "Service/gui/view/gui_service_view_screens.h"

#include "Service/gui/theme/gui_service_theme_config.h"

#define SERVICE_GUI_VIEW_LOCK_BREATH_TIME_MS      (1400U)  /* 解锁提示由亮变暗的时长。 */
#define SERVICE_GUI_VIEW_LOCK_BREATH_BACK_MS      (1500U)  /* 解锁提示由暗回亮的时长。 */
#define SERVICE_GUI_VIEW_LOCK_BREATH_MIN_OPA      (170)    /* 呼吸最暗时的整体不透明度。 */
#define SERVICE_GUI_VIEW_LOCK_UNLOCK_FADE_MS      (800U)   /* 解锁淡出到 Main 的时长。 */
#define SERVICE_GUI_VIEW_LOCK_UNLOCK_DELAY_MS     (100U)   /* 手势识别后到开始淡出的延迟。 */

/** @brief 解锁目标 Screen；由 create 记录。 */
static lv_obj_t *service_gui_view_lock_main;

/** @brief 底部解锁提示组，呼吸动画目标。 */
static lv_obj_t *service_gui_view_lock_unlock_group;

static void service_gui_view_lock_set_opa(void *object, int32_t opa)
{
    lv_obj_set_style_opa((lv_obj_t *)object, (lv_opa_t)opa, 0);
}

/**
 * @brief 让底部解锁提示开始无限呼吸。
 */
static void service_gui_view_lock_start_breath(void)
{
    lv_anim_t animation;

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, service_gui_view_lock_unlock_group);
    lv_anim_set_exec_cb(&animation, service_gui_view_lock_set_opa);
    lv_anim_set_values(&animation, LV_OPA_COVER, SERVICE_GUI_VIEW_LOCK_BREATH_MIN_OPA);
    lv_anim_set_time(&animation, SERVICE_GUI_VIEW_LOCK_BREATH_TIME_MS);
    lv_anim_set_playback_time(&animation, SERVICE_GUI_VIEW_LOCK_BREATH_BACK_MS);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_early_apply(&animation, false);
    lv_anim_start(&animation);
}

/**
 * @brief Lock Screen 事件：加载后开始呼吸；向上手势淡出到 Main。
 * @param[in] event Lock Screen 事件。
 */
static void service_gui_view_lock_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_SCREEN_LOADED)
    {
        service_gui_view_lock_start_breath();
    }
    else if ((code == LV_EVENT_GESTURE) &&
             (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP))
    {
        lv_indev_wait_release(lv_indev_get_act());
        lv_scr_load_anim(service_gui_view_lock_main,
                         LV_SCR_LOAD_ANIM_FADE_OUT,
                         SERVICE_GUI_VIEW_LOCK_UNLOCK_FADE_MS,
                         SERVICE_GUI_VIEW_LOCK_UNLOCK_DELAY_MS,
                         false);
    }
}

/**
 * @brief 创建一个 Ink 文字 Label。
 * @param[in] parent 父对象。
 * @param[in] text 文本。
 * @param[in] font 字体；为 NULL 时沿用主题字体。
 */
static lv_obj_t *service_gui_view_lock_create_label(lv_obj_t *parent,
                                                    const char *text,
                                                    const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_INK),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (font != NULL)
    {
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    return label;
}

/**
 * @brief 创建 Lock Screen。
 * @param[in,out] view 句柄集合；Main 必须已创建（解锁目标）。
 * @param[in] wallpaper 清晰系统壁纸。
 */
void service_gui_view_lock_create(Service_GUI_ViewTypeDef *view, const lv_img_dsc_t *wallpaper)
{
    lv_obj_t *lock;
    lv_obj_t *label;
    lv_obj_t *battery;
    lv_obj_t *bar;
    lv_obj_t *group;
    lv_obj_t *indicator;

    lock = lv_obj_create(NULL);
    lv_obj_clear_flag(lock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(lock, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(lock, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(lock, wallpaper, LV_PART_MAIN | LV_STATE_DEFAULT);

    label = service_gui_view_lock_create_label(lock, "10:42", &lv_font_montserrat_48);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(label, 0, lv_pct(-20));
    lv_obj_set_align(label, LV_ALIGN_CENTER);

    label = service_gui_view_lock_create_label(lock, "SUN, AUG 24", &lv_font_montserrat_16);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(label, 0, lv_pct(-5));
    lv_obj_set_align(label, LV_ALIGN_CENTER);

    battery = lv_obj_create(lock);
    lv_obj_remove_style_all(battery);
    lv_obj_set_size(battery, lv_pct(23), lv_pct(4));
    lv_obj_set_pos(battery, lv_pct(0), lv_pct(4));
    lv_obj_set_align(battery, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(battery, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(battery, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(battery, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(battery, 1, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 电池轮廓用 Ink Outline，填充用 Accent Indicator。 */
    bar = lv_bar_create(battery);
    lv_bar_set_value(bar, 84, LV_ANIM_OFF);
    lv_bar_set_start_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_size(bar, lv_pct(40), lv_pct(90));
    lv_obj_set_pos(bar, lv_pct(-30), lv_pct(-61));
    lv_obj_set_align(bar, LV_ALIGN_CENTER);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(bar, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_color(bar, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_INK),
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_opa(bar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(bar, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_pad(bar, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bar, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_ACCENT),
                              LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    label = service_gui_view_lock_create_label(battery, "84%", &lv_font_montserrat_12);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(label, 44, 16);
    lv_obj_set_align(label, LV_ALIGN_CENTER);

    group = lv_obj_create(lock);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, lv_pct(100), lv_pct(9));
    lv_obj_set_align(group, LV_ALIGN_BOTTOM_MID);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    label = service_gui_view_lock_create_label(group, "Swipe up to unlock", &lv_font_montserrat_10);
    lv_obj_set_align(label, LV_ALIGN_TOP_MID);

    indicator = lv_obj_create(group);
    lv_obj_set_size(indicator, lv_pct(16), lv_pct(10));
    lv_obj_set_pos(indicator, 0, lv_pct(-25));
    lv_obj_set_align(indicator, LV_ALIGN_BOTTOM_MID);
    lv_obj_clear_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(indicator, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_INK),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(indicator, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    service_gui_view_lock_main = view->main.screen;
    service_gui_view_lock_unlock_group = group;
    lv_obj_add_event_cb(lock, service_gui_view_lock_event, LV_EVENT_ALL, NULL);

    view->lock = lock;
}
