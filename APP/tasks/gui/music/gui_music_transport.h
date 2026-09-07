/**
  ******************************************************************************
  * @file    gui_music_transport.h
  * @brief   GUI Task 音乐分区私有的 paused/playing 与假进度策略。
  ******************************************************************************
  */

#ifndef GUI_MUSIC_TRANSPORT_H
#define GUI_MUSIC_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool playing;            /**< 有当前曲且用户已按播放；拔卡或空库必须为假。 */
    uint8_t progress_percent; /**< 0..100；切歌/拔卡归零，拖动不改 playing。 */
} GUI_MusicTransportTypeDef;

void gui_music_transport_init(GUI_MusicTransportTypeDef *transport);
bool gui_music_transport_toggle(
    GUI_MusicTransportTypeDef *transport,
    bool has_current);
bool gui_music_transport_force_paused(GUI_MusicTransportTypeDef *transport);
bool gui_music_transport_is_playing(const GUI_MusicTransportTypeDef *transport);
bool gui_music_transport_set_progress(
    GUI_MusicTransportTypeDef *transport,
    uint8_t percent);
bool gui_music_transport_reset_progress(GUI_MusicTransportTypeDef *transport);
uint8_t gui_music_transport_get_progress(
    const GUI_MusicTransportTypeDef *transport);

#endif /* GUI_MUSIC_TRANSPORT_H */
