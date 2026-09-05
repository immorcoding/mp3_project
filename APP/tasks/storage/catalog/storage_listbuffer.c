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
 * @brief 记下 Storage Task 句柄，并把窗口槽位复位为空闲。
 * @param[in] storage_task Storage Task 句柄，供 request 发 LISTBUFFER 通知；允许为 NULL（仅自测）。
 * @note 必须在 Storage Task 创建之后、首次 request 之前调用。不从 ISR 调用。
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
 * @brief GUI Task 在 SD 就绪且槽位 IDLE 时提交窗口请求。
 * @param[in] index_offset 播放列表起点，写入 Index；不是本窗条数。
 * @param[in] index_num 请求条数，范围 1..STORAGE_LISTBUFFER_MAX_ENTRIES，先写入 Length。
 * @param[in] generation Catalog/Sheet 代次。0 表示请求方无快照，load 按当前有效 Sheet 填窗。
 * @return STORAGE_OK 已写入 PENDING，并置 STORAGE_NOTIFY_FLAG_LISTBUFFER。
 * @retval STORAGE_ERROR SD 未就绪、条数非法、或槽位不是 IDLE。
 * @note 只允许 GUI Task 普通上下文调用，不得从 ISR 或 Service/gui 直接包含本模块。
 * @note load 成功后 Length 会改成实际填入条数，可能小于 index_num。当前播放游标不在本槽。
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
 * @return STORAGE_OK 已处理或槽位不是 PENDING（后者视为无事可做）。
 * @note 仅 Storage Task 在 SD 就绪路径调用。Sheet 代次为 0、与 Catalog 不一致、或与请求代次不一致（请求非 0）时，Length 与 Generation 置 0 仍打 READY，避免槽位卡在 PENDING。
 * @note 循环在列表末尾或拷贝失败处停止，Length 写成实际 filled；Buffer 其余槽位保持 memset 后的空串。
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
 * @note 拔卡或 SD 离开就绪时由 Storage Task 调用。Length 与 Generation 置 0；已是 IDLE/READY 则不动。
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
