/**
  ******************************************************************************
  * @file    gui_service_input.c
  * @brief   GUI 点击命令单槽：Process 写入，下一圈 Consume 取走。
  *          QUEUE_SELECT 与 SEEK 都走 param。
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
 * @param[in] param QUEUE_SELECT 为播放列表下标；SEEK 为 0..100 百分比。
 */
void service_gui_input_post(
    Service_GUI_InputCommandTypeDef command,
    uint16_t param)
{
    if (command == SERVICE_GUI_INPUT_NONE)
    {
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
