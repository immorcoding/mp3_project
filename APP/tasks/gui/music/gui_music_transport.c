/**
  ******************************************************************************
  * @file    gui_music_transport.c
  * @brief   音乐播放/暂停标志：不打开文件、不解码。
  ******************************************************************************
  */

#include "gui_music_transport.h"

#include <stddef.h>

/**
 * @brief 复位为 paused。
 * @param[out] transport 本分区持有的播放标志。
 */
void gui_music_transport_init(Gui_MusicTransportTypeDef *transport)
{
    if (transport == NULL)
    {
        return;
    }

    transport->playing = false;
}

/**
 * @brief 有当前曲时翻转 playing；否则保持 paused。
 * @param[in,out] transport 本分区持有的播放标志。
 * @param[in] has_current 播放列表游标当前有效。
 * @return 标志是否相对调用前发生变化。
 */
bool gui_music_transport_toggle(
    Gui_MusicTransportTypeDef *transport,
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
bool gui_music_transport_force_paused(Gui_MusicTransportTypeDef *transport)
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
bool gui_music_transport_is_playing(const Gui_MusicTransportTypeDef *transport)
{
    if (transport == NULL)
    {
        return false;
    }

    return transport->playing;
}
