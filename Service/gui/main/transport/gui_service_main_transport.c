/**
  ******************************************************************************
  * @file    gui_service_main_transport.c
  * @brief   Now Playing 三键：记下命令，并按 playing 换 PLAY/PAUSE 符号。
  ******************************************************************************
  */

#include "Service/gui/main/transport/gui_service_main_transport.h"
#include "Service/gui/gui_service_input.h"

#include "GUI/ui.h"
#include "lvgl.h"

static void service_gui_main_transport_on_clicked(lv_event_t *e);

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
    if (target == ui_MusicPreviousButton)
    {
        service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_PREVIOUS, 0U);
    }
    else if (target == ui_MusicPlayPauseButton)
    {
        service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_PLAY_PAUSE, 0U);
    }
    else if (target == ui_MusicNextButton)
    {
        service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_NEXT, 0U);
    }
}

/**
 * @brief 绑定 Now Playing 三键点击；不写 SquareLine 事件。
 * @retval SERVICE_OK 已绑定。
 * @retval SERVICE_NOT_READY 导出按钮或播放图标尚未创建。
 */
Service_StatusTypeDef service_gui_main_transport_prepare(void)
{
    if ((ui_MusicPreviousButton == NULL) ||
        (ui_MusicPlayPauseButton == NULL) ||
        (ui_MusicNextButton == NULL) ||
        (ui_MusicPlayPauseIcon == NULL))
    {
        return SERVICE_NOT_READY;
    }

    lv_obj_add_event_cb(
        ui_MusicPreviousButton,
        service_gui_main_transport_on_clicked,
        LV_EVENT_CLICKED,
        NULL);
    lv_obj_add_event_cb(
        ui_MusicPlayPauseButton,
        service_gui_main_transport_on_clicked,
        LV_EVENT_CLICKED,
        NULL);
    lv_obj_add_event_cb(
        ui_MusicNextButton,
        service_gui_main_transport_on_clicked,
        LV_EVENT_CLICKED,
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
    if (ui_MusicPlayPauseIcon == NULL)
    {
        return SERVICE_NOT_READY;
    }

    lv_label_set_text(
        ui_MusicPlayPauseIcon,
        playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    return SERVICE_OK;
}
