/**
 * @file storage_listbuffer.h
 * @brief Queue 窗口单槽：GUI request，Storage load。不向 GUI 暴露整表。
 */

#ifndef STORAGE_LISTBUFFER_H
#define STORAGE_LISTBUFFER_H

#include <stdint.h>

#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/storage/catalog/storage_catalog_config.h"

#define STORAGE_LISTBUFFER_IDLE    0U  /* 可接受下一次 request。 */
#define STORAGE_LISTBUFFER_PENDING 1U  /* GUI 已提交，等待 Storage load。 */
#define STORAGE_LISTBUFFER_READY   2U  /* 整窗已填完或已作废为空窗，待 GUI 读完后写回 IDLE。 */

typedef struct
{
    volatile uint32_t Status;     /* IDLE → PENDING → READY；GUI 整窗读完必须写回 IDLE。 */
    uint32_t Generation;          /* 与 Catalog/Sheet 同一代次；READY 且为 0 表示空窗/作废。 */
    uint16_t Index;               /* 窗口在播放列表上的起点；Buffer[0] 对应列表第 Index 首。 */
    uint16_t Length;              /* request 时为请求条数；READY 后为本窗实际条数，GUI 按此构造行。 */
    char Buffer[STORAGE_LISTBUFFER_MAX_ENTRIES][STORAGE_LISTBUFFER_PATH_BYTES];
} Storage_ListBufferTypeDef;

extern Storage_ListBufferTypeDef storage_listbuffer;

void storage_listbuffer_bind(void *storage_task);
Storage_StatusTypeDef storage_listbuffer_request(uint16_t index_offset,
                                                 uint16_t index_num,
                                                 uint32_t generation);
Storage_StatusTypeDef storage_listbuffer_load(void);
void storage_listbuffer_complete_unavailable(void);

#endif /* STORAGE_LISTBUFFER_H */
