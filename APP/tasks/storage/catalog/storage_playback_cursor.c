/**
 * @file storage_playback_cursor.c
 * @brief 播放列表游标：小状态在内部 RAM，不进 SDRAM 表，也不进窗口槽。
 */

#include "storage_playback_cursor.h"

#include <stddef.h>

/**
 * @brief 当前播放位置。
 * @note Generation 为 0 表示没有当前曲（作废或空库）。
 */
typedef struct
{
    uint32_t Generation;
    uint16_t Index;
    uint16_t Length;
} Storage_PlaybackCursorTypeDef;

static Storage_PlaybackCursorTypeDef storage_playback_cursor;

/**
 * @brief 按新表重建游标。
 * @param[in] index_num 播放列表有效长度，即当时的曲库条数。
 * @param[in] generation 对应的 Catalog/Sheet 代次。
 * @return STORAGE_OK。
 * @note 空库或代次为 0 时没有当前曲。非空库从下标 0 起。
 */
Storage_StatusTypeDef storage_playback_cursor_init(uint16_t index_num,
                                                   uint32_t generation)
{
    if ((index_num == 0U) || (generation == 0U))
    {
        storage_playback_cursor.Generation = 0U;
        storage_playback_cursor.Index = 0U;
        storage_playback_cursor.Length = 0U;
        return STORAGE_OK;
    }

    storage_playback_cursor.Index = 0U;
    storage_playback_cursor.Length = index_num;
    storage_playback_cursor.Generation = generation;
    return STORAGE_OK;
}

/**
 * @brief 作废游标。
 * @return STORAGE_OK。
 */
Storage_StatusTypeDef storage_playback_cursor_invalidate(void)
{
    storage_playback_cursor.Generation = 0U;
    storage_playback_cursor.Index = 0U;
    storage_playback_cursor.Length = 0U;
    return STORAGE_OK;
}

/**
 * @brief 读取当前播放列表下标与代次。
 * @param[out] index 播放列表下标。
 * @param[out] generation 与 Catalog/Sheet 相同的代次。
 * @retval STORAGE_OK 有当前曲。
 * @retval STORAGE_ERROR 已作废、空库或输出指针为空；不写输出。
 */
Storage_StatusTypeDef storage_playback_cursor_get(uint16_t *index,
                                                  uint32_t *generation)
{
    if ((index == NULL) || (generation == NULL) ||
        (storage_playback_cursor.Generation == 0U))
    {
        return STORAGE_ERROR;
    }

    *index = storage_playback_cursor.Index;
    *generation = storage_playback_cursor.Generation;
    return STORAGE_OK;
}

/**
 * @brief 把当前曲改到同一代次的播放列表下标。
 * @param[in] index 目标播放列表下标，须小于 init 时记下的库长。
 * @retval STORAGE_OK 已改当前下标，代次不变。
 * @retval STORAGE_ERROR 已作废、空库或下标越界；不改状态。
 * @note 不打开文件、不解码。Playback 打开/预开由调用方另接。
 */
Storage_StatusTypeDef storage_playback_cursor_set(uint16_t index)
{
    if ((storage_playback_cursor.Generation == 0U) ||
        (index >= storage_playback_cursor.Length))
    {
        return STORAGE_ERROR;
    }

    storage_playback_cursor.Index = index;
    return STORAGE_OK;
}

/**
 * @brief 把当前曲移到上一首；在 0 则环到末首。
 * @retval STORAGE_OK 已改当前下标，代次不变。
 * @retval STORAGE_ERROR 已作废或空库；不改状态。
 * @note 不打开文件、不解码。
 */
Storage_StatusTypeDef storage_playback_cursor_previous(void)
{
    if (storage_playback_cursor.Generation == 0U)
    {
        return STORAGE_ERROR;
    }

    if (storage_playback_cursor.Index == 0U)
    {
        storage_playback_cursor.Index =
            (uint16_t)(storage_playback_cursor.Length - 1U);
    }
    else
    {
        storage_playback_cursor.Index--;
    }

    return STORAGE_OK;
}

/**
 * @brief 把当前曲移到下一首；在末首则环到 0。
 * @retval STORAGE_OK 已改当前下标，代次不变。
 * @retval STORAGE_ERROR 已作废或空库；不改状态。
 * @note 不打开文件、不解码。
 */
Storage_StatusTypeDef storage_playback_cursor_next(void)
{
    if (storage_playback_cursor.Generation == 0U)
    {
        return STORAGE_ERROR;
    }

    storage_playback_cursor.Index++;
    if (storage_playback_cursor.Index >= storage_playback_cursor.Length)
    {
        storage_playback_cursor.Index = 0U;
    }

    return STORAGE_OK;
}
