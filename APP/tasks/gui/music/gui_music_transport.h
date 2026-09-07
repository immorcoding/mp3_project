/**
  ******************************************************************************
  * @file    gui_music_transport.h
  * @brief   GUI Task 音乐分区私有的 paused/playing 策略。
  ******************************************************************************
  */

#ifndef GUI_MUSIC_TRANSPORT_H
#define GUI_MUSIC_TRANSPORT_H

#include <stdbool.h>

typedef struct
{
    bool playing; /**< 有当前曲且用户已按播放；拔卡或空库必须为假。 */
} Gui_MusicTransportTypeDef;

void gui_music_transport_init(Gui_MusicTransportTypeDef *transport);
bool gui_music_transport_toggle(
    Gui_MusicTransportTypeDef *transport,
    bool has_current);
bool gui_music_transport_force_paused(Gui_MusicTransportTypeDef *transport);
bool gui_music_transport_is_playing(const Gui_MusicTransportTypeDef *transport);

#endif /* GUI_MUSIC_TRANSPORT_H */
