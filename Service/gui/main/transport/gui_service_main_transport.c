/**
  ******************************************************************************
  * @file    gui_service_main_transport.c
  * @brief   Now Playing 三键、进度条与时间 Label：记下命令，并按 playing、百分比与时间刷新。
  *
  * @details
  *          时间 Label 兼作状态行（GUI-19，#72 D1–D5）：本 Module 记下时间文本、常驻
  *          文本与跳过累加数，按“常驻文本 > 跳过提示 > 时间”决定 Label 显示什么，
  *          优先级只在这里实现。跳过提示用单次 lv_timer、闪烁用以 Label 为 var 的
  *          lv_anim，都按 LVGL tick 计时；Label 任一时刻至多一个提示计时器、一条闪烁动画。
  ******************************************************************************
  */

#include "Service/gui/main/transport/gui_service_main_transport.h"
#include "Service/gui/gui_service_config.h"
#include "Service/gui/gui_service_input.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Service/gui/view/gui_service_view.h"
#include "lvgl.h"

/** @brief 最长的时间文本（uint32_t 毫秒换成分钟后的上限），只用于确定缓冲大小。 */
#define SERVICE_GUI_MAIN_TRANSPORT_TIME_WIDEST  "71582:47/71582:47"

/** @brief 最长的跳过提示文本（累加数为 uint32_t 上限），只用于确定缓冲大小。 */
#define SERVICE_GUI_MAIN_TRANSPORT_SKIP_WIDEST  "Skipped 4294967295 files"

/** @brief Music 页对象句柄，prepare 时取得。 */
static const Service_GUI_ViewMusicTypeDef *service_gui_main_transport_music;

/** @brief 最近一次 TimeApply 的时间文本；状态行回到时间时写回 Label。 */
static char service_gui_main_transport_time_text[sizeof(SERVICE_GUI_MAIN_TRANSPORT_TIME_WIDEST)];

/** @brief 是否有常驻文本；常驻文本只存在 Label 里（Label 拷贝文本）。 */
static bool service_gui_main_transport_status_active;

/** @brief 本次跳过提示的累加数；0 表示没有提示。到期或被常驻文本取消时清零。 */
static uint32_t service_gui_main_transport_skip_count;

/** @brief 跳过提示的单次计时器；没有提示时为 NULL。到期回调里由 LVGL 删除。 */
static lv_timer_t *service_gui_main_transport_skip_timer;

static void service_gui_main_transport_on_clicked(lv_event_t *e);
static void service_gui_main_transport_on_slider_released(lv_event_t *e);
static void service_gui_main_transport_render(void);

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
 * @brief 把文本写进时间 Label；与 Label 现有文本相同时不重写，避免无谓重绘。
 * @param[in] text 要显示的文本，Label 会拷贝。
 * @note  调用方已确认 Label 存在。
 */
static void service_gui_main_transport_show(const char *text)
{
    lv_obj_t *label = service_gui_main_transport_music->time_label;

    if (strcmp(lv_label_get_text(label), text) == 0)
    {
        return;
    }

    lv_label_set_text(label, text);
}

/**
 * @brief 按“常驻文本 > 跳过提示 > 时间”刷新状态行（GUI-19、#72 D5）。
 * @note  常驻文本只存在 Label 里，有常驻文本时 Label 保持不动；否则显示跳过提示
 *        `Skipped 1 file` / `Skipped k files`，没有提示时显示最近一次的时间文本。
 */
static void service_gui_main_transport_render(void)
{
    char text[sizeof(SERVICE_GUI_MAIN_TRANSPORT_SKIP_WIDEST)];

    if (service_gui_main_transport_status_active)
    {
        return;
    }

    if (service_gui_main_transport_skip_count == 0U)
    {
        service_gui_main_transport_show(service_gui_main_transport_time_text);
        return;
    }

    (void)snprintf(text, sizeof(text), "Skipped %lu %s",
                   (unsigned long)service_gui_main_transport_skip_count,
                   (service_gui_main_transport_skip_count == 1U) ? "file" : "files");
    service_gui_main_transport_show(text);
}

/**
 * @brief 取消跳过提示：删除计时器、累加数清零；不刷新 Label。
 */
static void service_gui_main_transport_cancel_skip(void)
{
    if (service_gui_main_transport_skip_timer != NULL)
    {
        lv_timer_del(service_gui_main_transport_skip_timer);
        service_gui_main_transport_skip_timer = NULL;
    }

    service_gui_main_transport_skip_count = 0U;
}

/**
 * @brief 跳过提示到期：累加数清零，状态行回到时间。
 * @param[in] timer 到期的单次计时器；本回调返回后 LVGL 按重复次数 0 删除它。
 * @note 仅由 lv_timer_handler() 在 GUI Task 内调用。只清句柄、不自行删除，避免二次释放。
 */
static void service_gui_main_transport_on_skip_expired(lv_timer_t *timer)
{
    (void)timer;
    service_gui_main_transport_skip_timer = NULL;
    service_gui_main_transport_skip_count = 0U;
    service_gui_main_transport_render();
}

/**
 * @brief 闪烁动画每帧回调：把文字不透明度写给状态行 Label。
 * @param[in] var 动画 var，即时间 Label。
 * @param[in] value 文字不透明度 LV_OPA_TRANSP..LV_OPA_COVER。
 * @note 仅由 LVGL 动画定时器在 GUI Task 的 lv_timer_handler() 内调用。
 */
static void service_gui_main_transport_flash_exec(void *var, int32_t value)
{
    lv_obj_set_style_text_opa((lv_obj_t *)var, (lv_opa_t)value, LV_PART_MAIN | LV_STATE_DEFAULT);
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

    /* 状态行从时间开始；view 创建时不写文本，这里写入初始时间。 */
    service_gui_main_transport_status_active = false;
    service_gui_main_transport_skip_count = 0U;
    service_gui_main_transport_skip_timer = NULL;
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
 * @brief 按 playing 把播放键图标换成 PAUSE 或 PLAY，并按字形重新光学居中。
 * @param[in] playing 为真显示暂停符号。
 * @retval SERVICE_OK 已改文字与水平偏移。
 * @retval SERVICE_NOT_READY 图标对象尚未创建。
 * @note  播放三角形的质心偏左，右移 SERVICE_GUI_MUSIC_PLAY_ICON_SHIFT_X；暂停符号对称，
 *        回到包围盒居中。每次切换都重设，偏移不随旧字形残留。
 */
Service_StatusTypeDef service_gui_main_transport_apply(bool playing)
{
    lv_obj_t *icon;

    if ((service_gui_main_transport_music == NULL) ||
        (service_gui_main_transport_music->play_pause_icon == NULL))
    {
        return SERVICE_NOT_READY;
    }

    icon = service_gui_main_transport_music->play_pause_icon;
    lv_label_set_text(icon, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_obj_set_x(icon, playing ? 0 : SERVICE_GUI_MUSIC_PLAY_ICON_SHIFT_X);
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
 * @brief 判断状态行 Label 是否已可用。
 * @return 为真表示 prepare 已取得句柄且时间 Label 已创建。
 */
static bool service_gui_main_transport_label_ready(void)
{
    return (service_gui_main_transport_music != NULL) &&
           (service_gui_main_transport_music->time_label != NULL);
}

/**
 * @brief 记下“已解码/总时长”；状态行正显示时间时写入 Label，文本未变不重写。
 * @param[in] position_ms 已解码时间，按整秒向下取整显示。
 * @param[in] duration_ms 总时长；0 表示未知，显示 `--:--`。
 * @retval SERVICE_OK 已记下（并按优先级写入或暂不显示）。
 * @retval SERVICE_NOT_READY 时间 Label 尚未创建。
 * @note  不按墙钟走表，只显示调用方给出的快照时间。拖动进度条时照常写入真实时间。
 *        有常驻文本或跳过提示时只记下，等它们结束后再显示。
 */
Service_StatusTypeDef service_gui_main_transport_apply_time(uint32_t position_ms, uint32_t duration_ms)
{
    if (!service_gui_main_transport_label_ready())
    {
        return SERVICE_NOT_READY;
    }

    service_gui_main_transport_format_time(
        service_gui_main_transport_time_text,
        sizeof(service_gui_main_transport_time_text),
        position_ms,
        duration_ms);
    service_gui_main_transport_render();
    return SERVICE_OK;
}

/**
 * @brief 设置或取消状态行常驻文本（#72 D2）。
 * @param[in] text 非 NULL：常驻显示该文本（Label 拷贝），并取消正在进行的跳过提示；
 *            NULL：取消常驻，回到跳过提示（已被取消则无）或时间。
 * @retval SERVICE_OK 状态行已按优先级刷新。
 * @retval SERVICE_NOT_READY 时间 Label 尚未创建。
 * @note  不影响正在进行的闪烁。相同文本重复调用不重写 Label。
 */
Service_StatusTypeDef service_gui_main_transport_apply_status(const char *text)
{
    if (!service_gui_main_transport_label_ready())
    {
        return SERVICE_NOT_READY;
    }

    if (text == NULL)
    {
        service_gui_main_transport_status_active = false;
        service_gui_main_transport_render();
        return SERVICE_OK;
    }

    service_gui_main_transport_cancel_skip();
    service_gui_main_transport_status_active = true;
    service_gui_main_transport_show(text);
    return SERVICE_OK;
}

/**
 * @brief 累加跳过坏文件数并显示约 SERVICE_GUI_MUSIC_SKIP_NOTICE_MS 的提示（#72 D3）。
 * @param[in] added 本次新增的跳过数；0 不做任何事。
 * @retval SERVICE_OK 已累加并重新计时，或因常驻文本 / added 为 0 未处理。
 * @retval SERVICE_NOT_READY 时间 Label 尚未创建。
 * @retval SERVICE_ERROR LVGL 未能创建计时器；累加数与状态行都不变。
 * @note  有常驻文本时不显示也不累加（停止原因优先）。提示期间再调用则累加并从头
 *        计时；只用一个单次 lv_timer，按 LVGL tick 计时。累加数饱和在 uint32_t 上限。
 */
Service_StatusTypeDef service_gui_main_transport_apply_skip_notice(uint32_t added)
{
    if (!service_gui_main_transport_label_ready())
    {
        return SERVICE_NOT_READY;
    }

    if ((added == 0U) || service_gui_main_transport_status_active)
    {
        return SERVICE_OK;
    }

    if (service_gui_main_transport_skip_timer == NULL)
    {
        service_gui_main_transport_skip_timer = lv_timer_create(
            service_gui_main_transport_on_skip_expired,
            SERVICE_GUI_MUSIC_SKIP_NOTICE_MS,
            NULL);
        if (service_gui_main_transport_skip_timer == NULL)
        {
            return SERVICE_ERROR;
        }
        lv_timer_set_repeat_count(service_gui_main_transport_skip_timer, 1);
    }
    else
    {
        lv_timer_reset(service_gui_main_transport_skip_timer);
    }

    if (added > (UINT32_MAX - service_gui_main_transport_skip_count))
    {
        service_gui_main_transport_skip_count = UINT32_MAX;
    }
    else
    {
        service_gui_main_transport_skip_count += added;
    }

    service_gui_main_transport_render();
    return SERVICE_OK;
}

/**
 * @brief 让状态行闪一次：text_opa 在 SERVICE_GUI_MUSIC_STATUS_FLASH_MS 内淡出再淡回（#72 D4）。
 * @retval SERVICE_OK 闪烁已开始。
 * @retval SERVICE_NOT_READY 时间 Label 尚未创建。
 * @note  动画以 Label 为 var，先删同一 Label 上的旧闪烁并恢复不透明，再从头开始，
 *        所以重复调用只重启、不叠加。按 LVGL tick 计时，结束时回到 LV_OPA_COVER。
 */
Service_StatusTypeDef service_gui_main_transport_flash(void)
{
    lv_obj_t *label;
    lv_anim_t anim;

    if (!service_gui_main_transport_label_ready())
    {
        return SERVICE_NOT_READY;
    }

    label = service_gui_main_transport_music->time_label;
    (void)lv_anim_del(label, service_gui_main_transport_flash_exec);
    service_gui_main_transport_flash_exec(label, LV_OPA_COVER);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, label);
    lv_anim_set_exec_cb(&anim, service_gui_main_transport_flash_exec);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&anim, SERVICE_GUI_MUSIC_STATUS_FLASH_MS / 2U);
    lv_anim_set_playback_time(&anim, SERVICE_GUI_MUSIC_STATUS_FLASH_MS / 2U);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);
    return SERVICE_OK;
}
