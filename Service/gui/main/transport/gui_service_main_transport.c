/**
  ******************************************************************************
  * @file    gui_service_main_transport.c
  * @brief   Now Playing 三键、进度条与时间 Label：记下命令，并按 playing、百分比与时间刷新。
  ******************************************************************************
  */

#include "Service/gui/main/transport/gui_service_main_transport.h"
#include "Service/gui/gui_service_input.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Service/gui/view/gui_service_view.h"
#include "lvgl.h"

/** @brief 最长的时间文本（uint32_t 毫秒换成分钟后的上限），只用于确定缓冲大小。 */
#define SERVICE_GUI_MAIN_TRANSPORT_TIME_WIDEST  "71582:47/71582:47"

/** @brief Music 页对象句柄，prepare 时取得。 */
static const Service_GUI_ViewMusicTypeDef *service_gui_main_transport_music;

/** @brief 时间 Label 当前文本；文本不变时不重写 Label，避免每圈无谓重绘。 */
static char service_gui_main_transport_time_text[sizeof(SERVICE_GUI_MAIN_TRANSPORT_TIME_WIDEST)];

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
 * @brief 把毫秒换成 `m:ss` 写入缓冲。
 * @param[out] text 输出缓冲。
 * @param[in] size 缓冲字节数。
 * @param[in] ms 毫秒数；不足 1 s 的零头舍去。
 * @return snprintf 的返回值。
 */
static int service_gui_main_transport_format_clock(char *text, size_t size, uint32_t ms)
{
    const uint32_t seconds = ms / 1000U;

    return snprintf(text, size, "%lu:%02lu",
                    (unsigned long)(seconds / 60U),
                    (unsigned long)(seconds % 60U));
}

/**
 * @brief 按“已解码/总时长”格式化时间文本（C5）。
 * @param[out] text 输出缓冲，至少能放下 SERVICE_GUI_MAIN_TRANSPORT_TIME_WIDEST。
 * @param[in] size 缓冲字节数。
 * @param[in] position_ms 已解码时间。
 * @param[in] duration_ms 总时长；0 表示未知，写 `--:--`。
 */
static void service_gui_main_transport_format_time(
    char *text,
    size_t size,
    uint32_t position_ms,
    uint32_t duration_ms)
{
    int used;

    used = service_gui_main_transport_format_clock(text, size, position_ms);
    if ((used < 0) || ((size_t)used >= size))
    {
        return;
    }

    if (duration_ms == 0U)
    {
        (void)snprintf(text + used, size - (size_t)used, "/--:--");
        return;
    }

    text[used] = '/';
    used++;
    (void)service_gui_main_transport_format_clock(text + used, size - (size_t)used, duration_ms);
}

/**
 * @brief 绑定 Now Playing 三键点击与进度条松手，并写入初始时间 `0:00/--:--`；事件只在本 Module 绑定。
 * @retval SERVICE_OK 已绑定。
 * @retval SERVICE_NOT_READY 三键、播放图标、进度条或时间 Label 尚未创建。
 */
Service_StatusTypeDef service_gui_main_transport_prepare(void)
{
    service_gui_main_transport_music = &service_gui_view_get()->music;

    if ((service_gui_main_transport_music->previous_button == NULL) ||
        (service_gui_main_transport_music->play_pause_button == NULL) ||
        (service_gui_main_transport_music->next_button == NULL) ||
        (service_gui_main_transport_music->play_pause_icon == NULL) ||
        (service_gui_main_transport_music->slider == NULL) ||
        (service_gui_main_transport_music->time_label == NULL))
    {
        return SERVICE_NOT_READY;
    }

    /* 清空记下的文本，保证初始时间一定写进 Label（view 创建时不写文本）。 */
    service_gui_main_transport_time_text[0] = '\0';
    (void)service_gui_main_transport_apply_time(0U, 0U);

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

/**
 * @brief 按“已解码/总时长”改时间 Label；文本与上次相同时不重写。
 * @param[in] position_ms 已解码时间，按整秒向下取整显示。
 * @param[in] duration_ms 总时长；0 表示未知，显示 `--:--`。
 * @retval SERVICE_OK 已写入，或文本未变已跳过。
 * @retval SERVICE_NOT_READY 时间 Label 尚未创建。
 * @note  不按墙钟走表，只显示调用方给出的快照时间。拖动进度条时照常写入真实时间。
 */
Service_StatusTypeDef service_gui_main_transport_apply_time(uint32_t position_ms, uint32_t duration_ms)
{
    char text[sizeof(service_gui_main_transport_time_text)];

    if ((service_gui_main_transport_music == NULL) ||
        (service_gui_main_transport_music->time_label == NULL))
    {
        return SERVICE_NOT_READY;
    }

    service_gui_main_transport_format_time(text, sizeof(text), position_ms, duration_ms);
    if (strcmp(text, service_gui_main_transport_time_text) == 0)
    {
        return SERVICE_OK;
    }

    (void)memcpy(service_gui_main_transport_time_text, text, sizeof(text));
    lv_label_set_text(service_gui_main_transport_music->time_label, service_gui_main_transport_time_text);
    return SERVICE_OK;
}
