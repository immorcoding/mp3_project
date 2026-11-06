/**
 * @file catalog_sheet.c
 * @brief 顺序播放列表：把曲库下标写成恒等序列。
 */

#include "catalog_sheet.h"

#include "catalog_config.h"

#include <stdint.h>

/**
 * @brief 播放列表。
 * @note SeqList[i] 是曲库下标。首版恒等：SeqList[i] = i。
 *       有效长度不另存，就是曲库的条数。位于 SDRAM NOLOAD，启动不清零；有效与否由曲库代次决定，
 *       作废时不 memset 整表。
 */
typedef struct
{
    uint16_t SeqList[CATALOG_MUSIC_MAX_NUM];
    // uint16_t RandomList[CATALOG_MUSIC_MAX_NUM];
    // uint16_t HeartList[CATALOG_MUSIC_MAX_NUM]; //后续其他模式歌单
} Catalog_SheetTypeDef;

static Catalog_SheetTypeDef MusicSheet
__attribute__((section(".music_sheet"), aligned(32)));

/**
 * @brief 按曲库条数填写顺序下标。
 * @param[in] count 曲库当前条数；超过表容量的部分忽略。
 */
void catalog_sheet_fill(uint16_t count)
{
    for (uint16_t i = 0U; (i < count) && (i < CATALOG_MUSIC_MAX_NUM); i++)
    {
        MusicSheet.SeqList[i] = i;
    }
}

/**
 * @brief 把播放列表位置映射为曲库下标。
 * @param[in] position 播放列表位置。
 * @return 曲库下标；位置超出表容量返回 UINT16_MAX。
 */
uint16_t catalog_sheet_index(uint16_t position)
{
    if (position >= CATALOG_MUSIC_MAX_NUM)
    {
        return UINT16_MAX;
    }

    return MusicSheet.SeqList[position];
}
