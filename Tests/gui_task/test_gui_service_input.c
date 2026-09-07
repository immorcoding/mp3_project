/**
 ******************************************************************************
 * @file    test_gui_service_input.c
 * @brief   GUI 输入单槽的主机行为测试。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "Service/gui/gui_service.h"
#include "Service/gui/gui_service_input.h"

/**
 * @brief 空闲 Consume 把命令写成 NONE，并返回 OK。
 */
static void test_idle_consume_writes_none(void)
{
    Service_GUI_InputTypeDef input;

    input.command = SERVICE_GUI_INPUT_MUSIC_NEXT;
    input.sheet_index = 7U;

    assert(service_gui_input_consume(&input) == SERVICE_OK);
    assert(input.command == SERVICE_GUI_INPUT_NONE);
    assert(input.sheet_index == 0U);
}

/**
 * @brief 输出指针为空时拒绝。
 */
static void test_consume_rejects_null(void)
{
    assert(service_gui_input_consume(NULL) == SERVICE_INVALID_PARAM);
}

/**
 * @brief 取走一次 Queue 选曲后槽位变空。
 */
static void test_consume_takes_queue_select_once(void)
{
    Service_GUI_InputTypeDef input;

    service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_QUEUE_SELECT, 4U);
    input.command = SERVICE_GUI_INPUT_NONE;
    input.sheet_index = 0U;
    assert(service_gui_input_consume(&input) == SERVICE_OK);
    assert(input.command == SERVICE_GUI_INPUT_MUSIC_QUEUE_SELECT);
    assert(input.sheet_index == 4U);

    assert(service_gui_input_consume(&input) == SERVICE_OK);
    assert(input.command == SERVICE_GUI_INPUT_NONE);
}

/**
 * @brief 后一次 post 覆盖尚未取走的命令。
 */
static void test_later_post_overwrites_pending(void)
{
    Service_GUI_InputTypeDef input;

    service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_QUEUE_SELECT, 1U);
    service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_PLAY_PAUSE, 0U);
    assert(service_gui_input_consume(&input) == SERVICE_OK);
    assert(input.command == SERVICE_GUI_INPUT_MUSIC_PLAY_PAUSE);
}

/**
 * @brief 只丢掉 Queue 选曲，不误清播放键。
 */
static void test_drop_queue_select_keeps_other_commands(void)
{
    Service_GUI_InputTypeDef input;

    service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_PLAY_PAUSE, 0U);
    service_gui_input_drop_queue_select();
    assert(service_gui_input_consume(&input) == SERVICE_OK);
    assert(input.command == SERVICE_GUI_INPUT_MUSIC_PLAY_PAUSE);

    service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_QUEUE_SELECT, 2U);
    service_gui_input_drop_queue_select();
    assert(service_gui_input_consume(&input) == SERVICE_OK);
    assert(input.command == SERVICE_GUI_INPUT_NONE);
}

/**
 * @brief 运行全部输入单槽测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_idle_consume_writes_none();
    test_consume_rejects_null();
    test_consume_takes_queue_select_once();
    test_later_post_overwrites_pending();
    test_drop_queue_select_keeps_other_commands();

    puts("gui_service_input_tests: all tests passed");
    return 0;
}
