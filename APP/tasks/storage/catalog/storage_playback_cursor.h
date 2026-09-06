/**
 * @file storage_playback_cursor.h
 * @brief 播放列表游标：当前曲在列表上的下标，与 Catalog/Sheet 同代次。
 * @note  不放在 storage_listbuffer 里。不向 GUI 暴露整表。Service/gui 不得包含本头。
 */

#ifndef STORAGE_PLAYBACK_CURSOR_H
#define STORAGE_PLAYBACK_CURSOR_H

#include <stdint.h>

#include "APP/tasks/storage/storage_task.h"

Storage_StatusTypeDef storage_playback_cursor_init(uint16_t index_num,
                                                   uint32_t generation);
Storage_StatusTypeDef storage_playback_cursor_invalidate(void);
Storage_StatusTypeDef storage_playback_cursor_get(uint16_t *index,
                                                  uint32_t *generation);
Storage_StatusTypeDef storage_playback_cursor_set(uint16_t index);

#endif /* STORAGE_PLAYBACK_CURSOR_H */
