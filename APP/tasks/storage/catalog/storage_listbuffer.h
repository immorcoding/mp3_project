/**
 * @file storage_listbuffer.h
 * @brief Queue 窗口单槽：GUI request，Storage load。不向 GUI 暴露整表。
 */

#ifndef STORAGE_LISTBUFFER_H
#define STORAGE_LISTBUFFER_H

#include <stdint.h>

#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/storage/catalog/storage_catalog_config.h"

#define STORAGE_LISTBUFFER_IDLE    0U
#define STORAGE_LISTBUFFER_PENDING 1U
#define STORAGE_LISTBUFFER_READY   2U

typedef struct
{
    volatile uint32_t Status;
    uint32_t Generation;
    uint16_t Index;
    uint16_t Length;
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
