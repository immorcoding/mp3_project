/**
 * @file storage_namebuffer.c
 * @brief 曲目显示名单槽：GUI 音乐分区写入请求，Storage Task 按位置解析路径并提取文件主名。
 */

#include "storage_namebuffer.h"

#include "storage_catalog.h"
#include "storage_sheet.h"

#include <stdio.h>
#include <string.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include "Service/log/log_service.h"

/** @brief Storage Task 句柄；bind 复位槽位之后才发布，GUI 见到非 NULL 才可 request。 */
static TaskHandle_t volatile storage_namebuffer_task;

/** @brief load 时暂存 Catalog 路径；只由 Storage Task 使用，放静态区以免占任务栈。 */
static char storage_namebuffer_path[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];

Storage_NameBufferTypeDef storage_namebuffer;

/**
 * @brief 把槽位打成“取不到”：代次 0、空名、READY。
 * @note  仅 Storage Task 在槽位 PENDING 时调用。
 */
static void storage_namebuffer_complete_empty(void)
{
    storage_namebuffer.Buffer[0] = '\0';
    storage_namebuffer.Generation = 0U;
    storage_namebuffer.Status = STORAGE_NAMEBUFFER_READY;
}

/**
 * @brief 复位槽位为空闲，再记下 Storage Task 句柄。
 * @param[in] storage_task Storage Task 句柄，供 request 发 NAMEBUFFER 通知。
 * @note 必须在 Storage Task 创建之后调用；先复位、后发布句柄，GUI 在此之前的
 *       request 一律被拒（返回 STORAGE_ERROR，由调用方重试），不会被复位抹掉。不从 ISR 调用。
 */
void storage_namebuffer_bind(void *storage_task)
{
    storage_namebuffer.Status = STORAGE_NAMEBUFFER_IDLE;
    storage_namebuffer.Generation = 0U;
    storage_namebuffer.Index = 0U;
    storage_namebuffer.Buffer[0] = '\0';
    storage_namebuffer_task = (TaskHandle_t)storage_task;
}

/**
 * @brief GUI 音乐分区在槽位 IDLE 时提交名字请求。
 * @param[in] index 播放列表位置。
 * @param[in] generation 请求代次；0 表示“当前”，load 按当前有效 Sheet 应答并写回实际代次。
 * @return STORAGE_OK 已写入 PENDING，并置 STORAGE_NOTIFY_FLAG_NAMEBUFFER。
 * @retval STORAGE_ERROR Storage Task 尚未 bind，或槽位不是 IDLE。
 * @note 只允许 GUI Task 普通上下文调用，不得从 ISR 或 Service/gui 包含本模块。
 *       不检查 SD 是否就绪：未就绪时由 Storage 应答代次 0，GUI 据此显示 `--`。
 */
Storage_StatusTypeDef storage_namebuffer_request(uint16_t index, uint32_t generation)
{
    TaskHandle_t task = storage_namebuffer_task;

    if ((task == NULL) || (storage_namebuffer.Status != STORAGE_NAMEBUFFER_IDLE))
    {
        return STORAGE_ERROR;
    }

    storage_namebuffer.Index = index;
    storage_namebuffer.Generation = generation;
    storage_namebuffer.Status = STORAGE_NAMEBUFFER_PENDING;

    (void)xTaskNotifyIndexed(task,
                             STORAGE_NOTIFY_EVENT,
                             STORAGE_NOTIFY_FLAG_NAMEBUFFER,
                             eSetBits);
    return STORAGE_OK;
}

/**
 * @brief Storage Task 在 PENDING 时按 位置 → Sheet → Catalog 路径 提取文件主名并打 READY。
 * @param[in] sd_ready SD 是否处于就绪态；消抖中与未就绪均为假。
 * @note 仅 Storage Task 主循环调用；槽位不是 PENDING 时无事可做。
 * @note SD 未就绪、Sheet 无效或与 Catalog 不一致、请求代次非 0 且与当前不符，都是重扫与
 *       拔卡时的正常竞态：代次 0 空名，不记日志。代次相符（或请求为“当前”）仍取不到时是
 *       防御分支，记一条 WARN（只含位置与请求代次，不含名字）；请求为“当前”时越界属于空库
 *       开播的正常过程，不记。
 */
void storage_namebuffer_load(bool sd_ready)
{
    uint32_t sheet_generation;
    uint32_t requested_generation;
    uint16_t index;
    uint16_t catalog_index;
    bool extracted;
    char text[48];

    if (storage_namebuffer.Status != STORAGE_NAMEBUFFER_PENDING)
    {
        return;
    }

    requested_generation = storage_namebuffer.Generation;
    index = storage_namebuffer.Index;
    sheet_generation = storage_sheet_generation();

    if (!sd_ready || (sheet_generation == 0U) ||
        (sheet_generation != storage_catalog_generation()) ||
        ((requested_generation != 0U) && (requested_generation != sheet_generation)))
    {
        storage_namebuffer_complete_empty();
        return;
    }

    if (index >= storage_catalog_index_num())
    {
        if (requested_generation == 0U)
        {
            storage_namebuffer_complete_empty();
            return;
        }
        extracted = false;
    }
    else
    {
        catalog_index = storage_sheet_catalog_index(index);
        extracted =
            (catalog_index != UINT16_MAX) &&
            (storage_catalog_copy_path(catalog_index,
                                       storage_namebuffer_path,
                                       sizeof(storage_namebuffer_path)) == STORAGE_OK) &&
            storage_catalog_display_name(storage_namebuffer_path,
                                         (uint32_t)strlen(storage_namebuffer_path),
                                         storage_namebuffer.Buffer,
                                         sizeof(storage_namebuffer.Buffer));
    }

    if (!extracted)
    {
        storage_namebuffer_complete_empty();
        (void)snprintf(text, sizeof(text), "Name fail idx=%u gen=%lu",
                       (unsigned)index, (unsigned long)requested_generation);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, "STORAGE", text);
        return;
    }

    storage_namebuffer.Generation = sheet_generation;
    storage_namebuffer.Status = STORAGE_NAMEBUFFER_READY;
}

/**
 * @brief 若槽位仍为 PENDING，则打成代次 0 空名 READY，避免未就绪后槽位卡死。
 * @note 拔卡或 SD 离开就绪时由 Storage Task 调用；已是 IDLE/READY 则不动。不记日志。
 */
void storage_namebuffer_complete_unavailable(void)
{
    if (storage_namebuffer.Status != STORAGE_NAMEBUFFER_PENDING)
    {
        return;
    }

    storage_namebuffer_complete_empty();
}
