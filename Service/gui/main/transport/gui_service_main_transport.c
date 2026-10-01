/**
  ******************************************************************************
  * @file    gui_service_main_transport.c
  * @brief   Now Playing 三键与进度条：记下命令，并按 playing / 百分比刷新。
  ******************************************************************************
  */

#include "Service/gui/main/transport/gui_service_main_transport.h"
#include "Service/gui/gui_service_input.h"

#include <stdint.h>

#include "Service/gui/view/gui_service_view.h"
#include "lvgl.h"

/** @brief Music 页对象句柄，prepare 时取得。 */
static const Service_GUI_ViewMusicTypeDef *service_gui_main_transport_music;

static void service_gui_main_transport_on_clicked(lv_event_t *e);
static void service_gui_main_transport_on_slider_released(lv_event_t *e);

/**
 * @brief 三键 CLICKED 只 post 命令，不改游标或 playing。
 * @param[in] e LVGL 点按事件。
 */
static void service_gui_main_transport_on_clicked(lv_event_t *e)
{
    lv_obj_t *target;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    target = lv_event_get_current_target(e);
    if (target == service_gui_main_transport_music->previous_button)
    {
        service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_PREVIOUS, 0U);
    }
    else if (target == service_gui_main_transport_music->play_pause_button)
    {
        service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_PLAY_PAUSE, 0U);
    }
    else if (target == service_gui_main_transport_music->next_button)
    {
        service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_NEXT, 0U);
    }
}

/**
 * @brief 进度条松手只 post 百分比，不改 playing。
 * @param[in] e LVGL 松开事件。
 */
static void service_gui_main_transport_on_slider_released(lv_event_t *e)
{
    int32_t value;
    uint16_t percent;

    if (lv_event_get_code(e) != LV_EVENT_RELEASED)
    {
        return;
    }

    if (lv_event_get_current_target(e) != service_gui_main_transport_music->slider)
    {
        return;
    }

    value = lv_slider_get_value(service_gui_main_transport_music->slider);
    if (value < 0)
    {
        percent = 0U;
    }
    else if (value > 100)
    {
        percent = 100U;
    }
    else
    {
        percent = (uint16_t)value;
    }

    service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_SEEK, percent);
}

/**
 * @brief 绑定 Now Playing 三键点击与进度条松手；事件只在本 Module 绑定。
 * @retval SERVICE_OK 已绑定。
 * @retval SERVICE_NOT_READY 导出按钮、播放图标或进度条尚未创建。
 */
Service_StatusTypeDef service_gui_main_transport_prepare(void)
{
    service_gui_main_transport_music = &service_gui_view_get()->music;

    if ((service_gui_main_transport_music->previous_button == NULL) ||
        (service_gui_main_transport_music->play_pause_button == NULL) ||
        (service_gui_main_transport_music->next_button == NULL) ||
        (service_gui_main_transport_music->play_pause_icon == NULL) ||
        (service_gui_main_transport_music->slider == NULL))
    {
        return SERVICE_NOT_READY;
    }

    lv_obj_add_event_cb(
        service_gui_main_transport_music->previous_button,
        service_gui_main_transport_on_clicked,
        LV_EVENT_CLICKED,
        NULL);
    lv_obj_add_event_cb(
        service_gui_main_transport_music->play_pause_button,
        service_gui_main_transport_on_clicked,
        LV_EVENT_CLICKED,
        NULL);
    lv_obj_add_event_cb(
        service_gui_main_transport_music->next_button,
        service_gui_main_transport_on_clicked,
        LV_EVENT_CLICKED,
        NULL);
    lv_obj_add_event_cb(
        service_gui_main_transport_music->slider,
        service_gui_main_transport_on_slider_released,
        LV_EVENT_RELEASED,
        NULL);

    return SERVICE_OK;
}

/**
 * @brief 按 playing 把播放键图标换成 PAUSE 或 PLAY。
 * @param[in] playing 为真显示暂停符号。
 * @retval SERVICE_OK 已改文字。
 * @retval SERVICE_NOT_READY 图标对象尚未创建。
 */
Service_StatusTypeDef service_gui_main_transport_apply(bool playing)
{
    if ((service_gui_main_transport_music == NULL) ||
        (service_gui_main_transport_music->play_pause_icon == NULL))
    {
        return SERVICE_NOT_READY;
    }

    lv_label_set_text(
        service_gui_main_transport_music->play_pause_icon,
        playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    return SERVICE_OK;
}

/**
 * @brief 按百分比改进度条；手指按下拖动时不覆盖 LVGL 当前值。
 * @param[in] percent 0..100；大于 100 时夹到 100。
 * @retval SERVICE_OK 已写入，或拖动中已跳过。
 * @retval SERVICE_NOT_READY 进度条对象尚未创建。
 */
Service_StatusTypeDef service_gui_main_transport_apply_progress(uint8_t percent)
{
    if ((service_gui_main_transport_music == NULL) ||
        (service_gui_main_transport_music->slider == NULL))
    {
        return SERVICE_NOT_READY;
    }

    if (lv_obj_has_state(service_gui_main_transport_music->slider, LV_STATE_PRESSED))
    {
        return SERVICE_OK;
    }

    if (percent > 100U)
    {
        percent = 100U;
    }

    lv_slider_set_value(service_gui_main_transport_music->slider, (int32_t)percent, LV_ANIM_OFF);
    return SERVICE_OK;
}
