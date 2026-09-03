/**
 * @file storage_sheet.c
 * @brief 顺序播放列表：把 Catalog 下标写成恒等序列。
 */

#include "APP/tasks/storage/storage_task.h"
#include "storage_sheet.h"
#include "storage_catalog.h"

#include <stdint.h>

/**
 * @brief 播放列表。
 * @note SeqList[i] 是 Catalog 下标。首版恒等：SeqList[i] = i。
 *       有效长度不另存，就是建表时传入的 Catalog IndexNum。
 *       Generation 是对应的 Catalog 代次；0 表示表已作废。
 *       位于 SDRAM NOLOAD，启动不清零，作废时不 memset 整表。
 */
typedef struct{
    uint16_t SeqList[STORAGE_CATALOG_MUSIC_MAX_NUM];
    // uint16_t RandomList[STORAGE_CATALOG_MUSIC_MAX_NUM];
    // uint16_t HeartList[STORAGE_CATALOG_MUSIC_MAX_NUM]; //后续其他模式歌单
    uint32_t Generation;
}Storage_MusicSheetTypeDef;

static Storage_MusicSheetTypeDef MusicSheet
__attribute__((section(".music_sheet"), aligned(32)));

/**
 * @brief 按 Catalog 条数填写顺序下标，并记下 Catalog 代次。
 */
Storage_StatusTypeDef storage_sheet_init(uint16_t music_indexnum, uint32_t generation)
{
    for (uint16_t i = 0; i < music_indexnum; i++)
    {
        MusicSheet.SeqList[i] = i;
    }
    MusicSheet.Generation = generation;
    return STORAGE_OK;
}

/**
 * @brief 作废播放列表：代次置 0，不清 SeqList。
 */
Storage_StatusTypeDef storage_sheet_invalidate(void)
{
    MusicSheet.Generation = 0; /* 0 表示没有有效 Sheet，不会与 Catalog 代次（从 1 起）重合。 */
    return STORAGE_OK;
}