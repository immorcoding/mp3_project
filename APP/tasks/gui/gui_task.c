/**
  ******************************************************************************
  * @file    gui_task.c
  * @brief   GUI Task 的 FreeRTOS 运行循环。
  *
  * @details
 *          本任务是 LVGL 的唯一执行上下文。它先初始化 GUI Service，随后持续
 *          推进 LVGL 定时器、绘制与输入处理；SPI DMA 刷新期间的等待由 GUI
 *          Service 注册给 LVGL 的 wait callback 完成。同一循环里消费
 *          storage_listbuffer 窗口：先 Consume 点按选曲，再按 QueueTab 滚动
 *          更新 Index 后 request，READY 时 QueueApply 后写回 IDLE，不在
 *          Process 内包含该头。
  ******************************************************************************
  */

#include "gui_task.h"
#include "gui_task_queue_window.h"

#include <stddef.h>

#include "APP/tasks/storage/catalog/storage_listbuffer.h"
#include "APP/tasks/storage/catalog/storage_playback_cursor.h"
#include "Service/gui/gui_service.h"
#include "main.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

static Gui_QueueWindowClientTypeDef gui_task_queue_window_client;

/**
 * @brief 把 READY 窗口填进 Queue。
 * @return SERVICE_OK 已按 Length 填行或空窗。
 * @note  卷已卸载时不读 Buffer，填空窗以免画出已拔卡路径。当前 Buffer 仍是
 *        曲库路径；标题/歌手由以后的 load 调解析器写入。Label 会拷贝文本。
 *        当前行由播放列表游标判定，须与窗口代次一致。
 */
static Service_StatusTypeDef gui_task_apply_ready_window(void)
{
    const char *titles[STORAGE_LISTBUFFER_MAX_ENTRIES];
    uint16_t length;
    uint16_t i;
    uint16_t cursor_index = 0U;
    uint32_t cursor_generation = 0U;
    uint16_t current_index;
    bool cursor_valid;

    if (!storage_task_sd_is_mounted())
    {
        return Service_GUI_QueueApply(NULL, 0U, 0U, SERVICE_GUI_QUEUE_NO_CURRENT);
    }

    length = storage_listbuffer.Length;
    if (length > STORAGE_LISTBUFFER_MAX_ENTRIES)
    {
        length = STORAGE_LISTBUFFER_MAX_ENTRIES;
    }

    for (i = 0U; i < length; i++)
    {
        titles[i] = storage_listbuffer.Buffer[i];
    }

    cursor_valid = (storage_playback_cursor_get(&cursor_index, &cursor_generation) ==
                    STORAGE_OK);
    current_index = gui_task_queue_window_current_index(
        cursor_valid,
        cursor_index,
        cursor_generation,
        storage_listbuffer.Generation);

    if (length == 0U)
    {
        return Service_GUI_QueueApply(NULL, 0U, 0U, current_index);
    }

    return Service_GUI_QueueApply(
        titles,
        length,
        storage_listbuffer.Index,
        current_index);
}

/**
 * @brief  初始化 GUI Service 并持续推进唯一的 LVGL 执行上下文。
 * @param  handle 未使用；保留以符合 FreeRTOS TaskFunction_t 签名。
 * @note   GUI 初始化失败被当前产品定义为致命错误，任务会进入 Error_Handler()。
 *         正常路径不返回，所有 LVGL API（LCD 最终 ISR 的 flush ready 例外除外）
 *         均由本 Task 调用。窗口协议每圈轮询一次，不阻塞等待 READY。
 */
void gui_task(void *handle)
{
    (void)handle;

    _Static_assert((unsigned)GUI_NOTIFY_COUNT <=
                       (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES,
                   "GUI Task notify slots exceed FreeRTOS array length");
    _Static_assert(GUI_TASK_QUEUE_WINDOW_IDLE == STORAGE_LISTBUFFER_IDLE,
                   "queue window IDLE must match listbuffer");
    _Static_assert(GUI_TASK_QUEUE_WINDOW_PENDING == STORAGE_LISTBUFFER_PENDING,
                   "queue window PENDING must match listbuffer");
    _Static_assert(GUI_TASK_QUEUE_WINDOW_READY == STORAGE_LISTBUFFER_READY,
                   "queue window READY must match listbuffer");
    _Static_assert(GUI_TASK_QUEUE_WINDOW_NO_CURRENT == SERVICE_GUI_QUEUE_NO_CURRENT,
                   "queue window no-current must match QueueApply sentinel");

    gui_task_queue_window_client_init(&gui_task_queue_window_client);

    if (Service_GUI_Init(GUI_NOTIFY_LCD_TRANSFER) != SERVICE_OK)
    {
        Error_Handler();
    }

    while (1)
    {
        Gui_QueueWindowActionTypeDef action;
        uint16_t selected_index;

        if (Service_GUI_QueueConsumeSelect(&selected_index) == SERVICE_OK)
        {
            (void)storage_playback_cursor_set(selected_index);
            /*
             * Playback Task（未落地）
             * ------------------------------------------------------------
             * 游标已改到 selected_index。此处应通知后台播放进程打开/预开
             * 该播放列表位置的曲目，不要在 GUI Task 里读文件或解码。
             * ------------------------------------------------------------
             */
        }

        gui_task_queue_window_note_lead(
            &gui_task_queue_window_client,
            Service_GUI_QueueScrollLead(),
            STORAGE_LISTBUFFER_MAX_ENTRIES);

        action = gui_task_queue_window_poll(
            &gui_task_queue_window_client,
            storage_task_sd_is_ready(),
            storage_task_sd_is_mounted(),
            storage_listbuffer.Status);

        switch (action)
        {
            case GUI_TASK_QUEUE_WINDOW_ACTION_NONE:
                break;

            case GUI_TASK_QUEUE_WINDOW_ACTION_REQUEST:
                (void)storage_listbuffer_request(
                    gui_task_queue_window_request_index(
                        &gui_task_queue_window_client),
                    STORAGE_LISTBUFFER_MAX_ENTRIES,
                    0U);
                break;

            case GUI_TASK_QUEUE_WINDOW_ACTION_APPLY:
                if ((gui_task_apply_ready_window() == SERVICE_OK) &&
                    storage_task_sd_is_mounted())
                {
                    uint16_t applied_index;
                    uint16_t applied_length;

                    applied_length = storage_listbuffer.Length;
                    applied_index = storage_listbuffer.Index;
                    if (applied_length == 0U)
                    {
                        applied_index = 0U;
                    }
                    else if (applied_length > STORAGE_LISTBUFFER_MAX_ENTRIES)
                    {
                        applied_length = STORAGE_LISTBUFFER_MAX_ENTRIES;
                    }

                    gui_task_queue_window_mark_applied(
                        &gui_task_queue_window_client,
                        applied_index,
                        applied_length);
                }

                storage_listbuffer.Status = STORAGE_LISTBUFFER_IDLE;
                break;

            case GUI_TASK_QUEUE_WINDOW_ACTION_CLEAR:
                (void)Service_GUI_QueueApply(NULL, 0U, 0U, SERVICE_GUI_QUEUE_NO_CURRENT);
                break;

            default:
                break;
        }

        Service_GUI_Process();
    }
}
