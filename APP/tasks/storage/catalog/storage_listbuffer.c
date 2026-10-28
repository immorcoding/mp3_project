/**
 * @file storage_listbuffer.c
 * @brief Queue 窗口单槽：GUI 写入请求，Storage Task 逐圈读一行曲目标签，整窗填完才打 READY。
 */

#include "storage_listbuffer.h"

#include "storage_catalog.h"
#include "storage_sheet.h"
#include "storage_track_diag.h"
#include "storage_track_text.h"

#include <stdbool.h>
#include <string.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/** @brief 一窗填写的进度；只由 Storage Task 读写，GUI 看不到。 */
typedef struct
{
    bool Started;        /* 本次 PENDING 已通过代次检查并开始逐行填写。 */
    uint32_t Generation; /* 开始填写时的 Sheet 代次；途中变化即以空窗收尾。 */
    uint16_t Filled;     /* 已填好的行数，也是下一行的行号。 */
} Storage_ListBufferFillTypeDef;

static TaskHandle_t storage_listbuffer_task;

/** @brief 当前填写进度；READY 或空窗收尾时复位。 */
static Storage_ListBufferFillTypeDef storage_listbuffer_fill;

/** @brief 当前行的曲库相对路径拷贝；只用于打开文件，不进槽（STOR-16）。 */
static char storage_listbuffer_path[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];

Storage_ListBufferTypeDef storage_listbuffer;

/**
 * @brief 打成空窗 READY（代次 0），并复位填写进度。
 * @note  只由 Storage Task 调用。
 */
static void storage_listbuffer_complete_empty(void)
{
    storage_listbuffer_fill.Started = false;
    storage_listbuffer.Length = 0U;
    storage_listbuffer.Generation = 0U;
    storage_listbuffer.Status = STORAGE_LISTBUFFER_READY;
}

/**
 * @brief 记下 Storage Task 句柄，并把窗口槽位复位为空闲。
 * @param[in] storage_task Storage Task 句柄，供 request 发 LISTBUFFER 通知；允许为 NULL（仅自测）。
 * @note 必须在 Storage Task 创建之后、首次 request 之前调用。不从 ISR 调用。
 */
void storage_listbuffer_bind(void *storage_task)
{
    storage_listbuffer_task = (TaskHandle_t)storage_task;
    storage_listbuffer_fill.Started = false;
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
 * @brief Storage Task 在 PENDING 时最多读一行曲目标签；整窗填完才打 READY。
 * @return STORAGE_OK 已处理或槽位不是 PENDING（后者视为无事可做）。
 * @note 仅 Storage Task 在 SD 就绪路径调用，每圈一次（STOR-16）。首圈检查代次：Sheet 代次为 0、
 *       与 Catalog 不一致、或与请求代次不一致（请求非 0）时打成空窗 READY。之后每圈先核对 Sheet
 *       代次仍是开始时的代次，变化则以空窗（代次 0）收尾，GUI 看不到半窗。
 * @note 每行经 storage_track_text_read() 写曲目显示名与歌手；标签侧失败只令该行回退文件主名、
 *       歌手为空，不中断整窗；文件主名也提取不出时该行两列为空串（防御分支）。列表末尾、
 *       Sheet 映射或路径拷贝失败处停止，Length 写成实际填好的行数。
 * @note 每圈最坏一次 f_open + 取长度 + 至多 STORAGE_CATALOG_TEXT_MAX_READS 次读取 + f_close。
 */
Storage_StatusTypeDef storage_listbuffer_load(void)
{
    Storage_ListBufferRowTypeDef *row;
    const StorageTrackText_ResultTypeDef *track;
    uint32_t sheet_generation;
    uint32_t list_index;
    uint32_t start_cycles;
    uint16_t catalog_index;
    bool stop;

    if (storage_listbuffer.Status != STORAGE_LISTBUFFER_PENDING)
    {
        return STORAGE_OK;
    }

    sheet_generation = storage_sheet_generation();

    if (!storage_listbuffer_fill.Started)
    {
        if ((sheet_generation == 0U) || (sheet_generation != storage_catalog_generation()) ||
            ((storage_listbuffer.Generation != 0U) &&
             (storage_listbuffer.Generation != sheet_generation)))
        {
            storage_listbuffer_complete_empty();
            return STORAGE_OK;
        }

        storage_listbuffer_fill.Started = true;
        storage_listbuffer_fill.Generation = sheet_generation;
        storage_listbuffer_fill.Filled = 0U;
        storage_track_diag_window_begin();
    }
    else if ((sheet_generation != storage_listbuffer_fill.Generation) ||
             (sheet_generation != storage_catalog_generation()))
    {
        storage_listbuffer_complete_empty();
        return STORAGE_OK;
    }

    list_index = (uint32_t)storage_listbuffer.Index + storage_listbuffer_fill.Filled;
    stop = (storage_listbuffer_fill.Filled >= storage_listbuffer.Length) ||
           (list_index >= storage_catalog_index_num());

    if (!stop)
    {
        catalog_index = storage_sheet_catalog_index((uint16_t)list_index);
        stop = (catalog_index == UINT16_MAX) ||
               (storage_catalog_copy_path(catalog_index,
                                          storage_listbuffer_path,
                                          sizeof(storage_listbuffer_path)) != STORAGE_OK);
    }

    if (!stop)
    {
        row = &storage_listbuffer.Rows[storage_listbuffer_fill.Filled];
        start_cycles = storage_track_diag_begin();
        track = storage_track_text_read(storage_listbuffer_path);
        storage_track_diag_row_done(start_cycles, track);
        if (track != NULL)
        {
            (void)memcpy(row->Title, track->Text.Title, sizeof(row->Title));
            (void)memcpy(row->Artist, track->Text.Artist, sizeof(row->Artist));
        }
        else
        {
            row->Title[0] = '\0';
            row->Artist[0] = '\0';
        }

        storage_listbuffer_fill.Filled++;
        list_index++;
        stop = (storage_listbuffer_fill.Filled >= storage_listbuffer.Length) ||
               (list_index >= storage_catalog_index_num());
    }

    if (!stop)
    {
        return STORAGE_OK;
    }

    storage_track_diag_window_end();
    storage_listbuffer.Length = storage_listbuffer_fill.Filled;
    storage_listbuffer.Generation = storage_listbuffer_fill.Generation;
    storage_listbuffer_fill.Started = false;
    storage_listbuffer.Status = STORAGE_LISTBUFFER_READY;
    return STORAGE_OK;
}

/**
 * @brief 若窗口仍为 PENDING（含填到一半），则打成空窗 READY，避免未就绪后槽位卡死。
 * @note 拔卡或 SD 离开就绪时由 Storage Task 调用。Length 与 Generation 置 0；已是 IDLE/READY 则不动。
 */
void storage_listbuffer_complete_unavailable(void)
{
    if (storage_listbuffer.Status != STORAGE_LISTBUFFER_PENDING)
    {
        return;
    }

    storage_listbuffer_complete_empty();
}
