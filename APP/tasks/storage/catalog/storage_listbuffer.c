/**
 * @file storage_listbuffer.c
 * @brief Queue 窗口单槽：GUI 写入请求，Storage Task 填路径拷贝。
 */

#include "storage_listbuffer.h"

#include "storage_catalog.h"
#include "storage_sheet.h"

#include <string.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

static TaskHandle_t storage_listbuffer_task;

Storage_ListBufferTypeDef storage_listbuffer;

/**
 * @brief 记下 Storage Task 句柄，供 request 叫醒填窗。
 */
void storage_listbuffer_bind(void *storage_task)
{
    storage_listbuffer_task = (TaskHandle_t)storage_task;
    storage_listbuffer.Status = STORAGE_LISTBUFFER_IDLE;
    storage_listbuffer.Generation = 0U;
    storage_listbuffer.Index = 0U;
    storage_listbuffer.Length = 0U;
}

/**
 * @brief GUI Task 在 SD 就绪且 IDLE 时写入窗口请求并打成 PENDING。
 */
Storage_StatusTypeDef storage_listbuffer_request(uint16_t index_offset,
                                                 uint16_t index_num,
                                                 uint32_t generation)
{
    if (!storage_task_sd_is_ready())
    {
        return STORAGE_ERROR;
    }

    if ((index_num == 0U) || (index_num > STORAGE_LISTBUFFER_MAX_ENTRIES))
    {
        return STORAGE_ERROR;
    }

    if (storage_listbuffer.Status != STORAGE_LISTBUFFER_IDLE)
    {
        return STORAGE_ERROR;
    }

    storage_listbuffer.Index = index_offset;
    storage_listbuffer.Length = index_num;
    storage_listbuffer.Generation = generation;
    storage_listbuffer.Status = STORAGE_LISTBUFFER_PENDING;

    if (storage_listbuffer_task != NULL)
    {
        (void)xTaskNotifyIndexed(storage_listbuffer_task,
                                 STORAGE_NOTIFY_EVENT,
                                 STORAGE_NOTIFY_FLAG_LISTBUFFER,
                                 eSetBits);
    }

    return STORAGE_OK;
}

/**
 * @brief Storage Task 在 PENDING 时按代次填整窗路径拷贝，最后打成 READY。
 */
Storage_StatusTypeDef storage_listbuffer_load(void)
{
    uint32_t sheet_generation;
    uint32_t catalog_generation;
    uint16_t catalog_count;
    uint16_t requested;
    uint16_t start;
    uint16_t filled;
    uint16_t i;

    if (storage_listbuffer.Status != STORAGE_LISTBUFFER_PENDING)
    {
        return STORAGE_OK;
    }

    sheet_generation = storage_sheet_generation();
    catalog_generation = storage_catalog_generation();
    catalog_count = storage_catalog_index_num();
    requested = storage_listbuffer.Length;
    start = storage_listbuffer.Index;
    filled = 0U;

    if ((sheet_generation == 0U) || (sheet_generation != catalog_generation) ||
        ((storage_listbuffer.Generation != 0U) &&
         (storage_listbuffer.Generation != sheet_generation)))
    {
        storage_listbuffer.Length = 0U;
        storage_listbuffer.Generation = 0U;
        storage_listbuffer.Status = STORAGE_LISTBUFFER_READY;
        return STORAGE_OK;
    }

    (void)memset(storage_listbuffer.Buffer, 0, sizeof(storage_listbuffer.Buffer));

    for (i = 0U; (i < requested) && ((uint32_t)start + i < catalog_count); i++)
    {
        uint16_t catalog_index;

        catalog_index = storage_sheet_catalog_index((uint16_t)(start + i));
        if (catalog_index == UINT16_MAX)
        {
            break;
        }

        if (storage_catalog_copy_path(catalog_index,
                                      storage_listbuffer.Buffer[i],
                                      STORAGE_LISTBUFFER_PATH_BYTES) != STORAGE_OK)
        {
            break;
        }

        filled++;
    }

    storage_listbuffer.Length = filled;
    storage_listbuffer.Generation = sheet_generation;
    storage_listbuffer.Status = STORAGE_LISTBUFFER_READY;
    return STORAGE_OK;
}

/**
 * @brief 若窗口仍为 PENDING，则打成空窗 READY，避免未就绪后槽位卡死。
 */
void storage_listbuffer_complete_unavailable(void)
{
    if (storage_listbuffer.Status != STORAGE_LISTBUFFER_PENDING)
    {
        return;
    }

    storage_listbuffer.Length = 0U;
    storage_listbuffer.Generation = 0U;
    storage_listbuffer.Status = STORAGE_LISTBUFFER_READY;
}
