/**
  ******************************************************************************
  * @file    gui_music_transport.c
  * @brief   音乐播放/暂停标志与假进度：不打开文件、不解码。
  ******************************************************************************
  */

#include "gui_music_transport.h"

#include <stddef.h>

/**
 * @brief 复位为 paused。
 * @param[out] transport 本分区持有的播放标志。
 */
void gui_music_transport_init(GUI_MusicTransportTypeDef *transport)
{
    if (transport == NULL)
    {
        return;
    }

    transport->playing = false;
    transport->progress_percent = 0U;
}

/**
 * @brief 有当前曲时翻转 playing；否则保持 paused。
 * @param[in,out] transport 本分区持有的播放标志。
 * @param[in] has_current 播放列表游标当前有效。
 * @return 标志是否相对调用前发生变化。
 */
bool gui_music_transport_toggle(
    GUI_MusicTransportTypeDef *transport,
    bool has_current)
{
    bool was_playing;

    if (transport == NULL)
    {
        return false;
    }

    was_playing = transport->playing;
    if (!has_current)
    {
        transport->playing = false;
        return was_playing;
    }

    transport->playing = !transport->playing;
    return true;
}

/**
 * @brief 强制 paused，供拔卡清空 Queue 时使用。
 * @param[in,out] transport 本分区持有的播放标志。
 * @return 调用前是否为 playing。
 */
bool gui_music_transport_force_paused(GUI_MusicTransportTypeDef *transport)
{
    bool was_playing;

    if (transport == NULL)
    {
        return false;
    }

    was_playing = transport->playing;
    transport->playing = false;
    return was_playing;
}

/**
 * @brief 读取是否为 playing。
 * @param[in] transport 本分区持有的播放标志。
 * @return playing 为真；空指针视为 paused。
 */
bool gui_music_transport_is_playing(const GUI_MusicTransportTypeDef *transport)
{
    if (transport == NULL)
    {
        return false;
    }

    return transport->playing;
}

/**
 * @brief 记下进度；超过 100 夹到 100。
 * @param[in,out] transport 本分区持有的播放标志与进度。
 * @param[in] percent 目标百分比。
 * @return 百分比是否相对调用前发生变化。
 */
bool gui_music_transport_set_progress(
    GUI_MusicTransportTypeDef *transport,
    uint8_t percent)
{
    uint8_t clamped;

    if (transport == NULL)
    {
        return false;
    }

    clamped = (percent > 100U) ? 100U : percent;
    if (transport->progress_percent == clamped)
    {
        return false;
    }

    transport->progress_percent = clamped;
    return true;
}

/**
 * @brief 把进度清回 0。
 * @param[in,out] transport 本分区持有的播放标志与进度。
 * @return 调用前百分比是否非 0。
 */
bool gui_music_transport_reset_progress(GUI_MusicTransportTypeDef *transport)
{
    bool changed;

    if (transport == NULL)
    {
        return false;
    }

    changed = (transport->progress_percent != 0U);
    transport->progress_percent = 0U;
    return changed;
}

/**
 * @brief 读取进度百分比。
 * @param[in] transport 本分区持有的播放标志与进度。
 * @return 0..100；空指针视为 0。
 */
uint8_t gui_music_transport_get_progress(
    const GUI_MusicTransportTypeDef *transport)
{
    if (transport == NULL)
    {
        return 0U;
    }

    return transport->progress_percent;
}
