/**
  ******************************************************************************
  * @file    gui_service_input.c
  * @brief   GUI 点击命令单槽：Process 写入，下一圈 Consume 取走。
  *          QUEUE_SELECT 与进度条手势（SEEK_PRESS/DRAG/RELEASE）走 param。
  ******************************************************************************
  */

#include "Service/gui/gui_service_input.h"

#include <stddef.h>

static bool service_gui_input_pending;
static Service_GUI_InputCommandTypeDef service_gui_input_command;
static uint16_t service_gui_input_param;

/**
 * @brief 记下一次待取走的输入命令；尚未取走则覆盖。
 * @param[in] command 点击对应的命令，不得为 NONE。
 * @param[in] param QUEUE_SELECT 为播放列表下标；SEEK_PRESS/DRAG/RELEASE 为千分比。
 * @note  唯一的合并：尚未取走的 SEEK_PRESS 遇到 SEEK_DRAG 时保留按下、比例取拖动的值。
 *        LVGL 同一次读触摸先发 PRESSED 再发 PRESSING，不合并按下就会被拖动覆盖。
 */
void service_gui_input_post(
    Service_GUI_InputCommandTypeDef command,
    uint16_t param)
{
    if (command == SERVICE_GUI_INPUT_NONE)
    {
        return;
    }

    if (service_gui_input_pending &&
        (service_gui_input_command == SERVICE_GUI_INPUT_MUSIC_SEEK_PRESS) &&
        (command == SERVICE_GUI_INPUT_MUSIC_SEEK_DRAG))
    {
        service_gui_input_param = param;
        return;
    }

    service_gui_input_command = command;
    service_gui_input_param = param;
    service_gui_input_pending = true;
}

/**
 * @brief 空窗时丢掉尚未取走的 Queue 选曲，其它命令保留。
 */
void service_gui_input_drop_queue_select(void)
{
    if (service_gui_input_pending &&
        (service_gui_input_command == SERVICE_GUI_INPUT_MUSIC_QUEUE_SELECT))
    {
        service_gui_input_pending = false;
        service_gui_input_command = SERVICE_GUI_INPUT_NONE;
        service_gui_input_param = 0U;
    }
}

/**
 * @brief 输入槽里是否有尚未取走的命令。
 * @return 有待取走的命令为 true。
 * @note  只读，不取走。Service_GUI_Process() 用它决定本圈结束后只睡下限（GUI-28），
 *        让下一圈马上 Consume，松手到出声的路径不多等一个刷新周期。
 */
bool service_gui_input_has_pending(void)
{
    return service_gui_input_pending;
}

/**
 * @brief 取走一次输入；无点击时写成 NONE 并返回 OK。
 * @param[out] input 命令与可选 param。
 * @retval SERVICE_OK 已写入 input。
 * @retval SERVICE_INVALID_PARAM input 为空。
 */
Service_StatusTypeDef service_gui_input_consume(Service_GUI_InputTypeDef *input)
{
    if (input == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }

    if (!service_gui_input_pending)
    {
        input->command = SERVICE_GUI_INPUT_NONE;
        input->param = 0U;
        return SERVICE_OK;
    }

    input->command = service_gui_input_command;
    input->param = service_gui_input_param;
    service_gui_input_pending = false;
    service_gui_input_command = SERVICE_GUI_INPUT_NONE;
    service_gui_input_param = 0U;
    return SERVICE_OK;
}
