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
    input.param = 7U;

    assert(service_gui_input_consume(&input) == SERVICE_OK);
    assert(input.command == SERVICE_GUI_INPUT_NONE);
    assert(input.param == 0U);
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
    input.param = 0U;
    assert(service_gui_input_consume(&input) == SERVICE_OK);
    assert(input.command == SERVICE_GUI_INPUT_MUSIC_QUEUE_SELECT);
    assert(input.param == 4U);

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
 * @brief 取走一次进度条松手后槽位变空，param 为千分比。
 */
static void test_consume_takes_seek_release_once(void)
{
    Service_GUI_InputTypeDef input;

    service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_SEEK_RELEASE, 375U);
    input.command = SERVICE_GUI_INPUT_NONE;
    input.param = 0U;
    assert(service_gui_input_consume(&input) == SERVICE_OK);
    assert(input.command == SERVICE_GUI_INPUT_MUSIC_SEEK_RELEASE);
    assert(input.param == 375U);

    assert(service_gui_input_consume(&input) == SERVICE_OK);
    assert(input.command == SERVICE_GUI_INPUT_NONE);
}

/**
 * @brief 按下尚未取走时又来拖动：保留按下，比例取拖动的最新值（同一次读触摸先 PRESSED 再 PRESSING）。
 */
static void test_drag_after_pending_press_keeps_press_with_latest_value(void)
{
    Service_GUI_InputTypeDef input;

    service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_SEEK_PRESS, 100U);
    service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_SEEK_DRAG, 420U);
    assert(service_gui_input_consume(&input) == SERVICE_OK);
    assert(input.command == SERVICE_GUI_INPUT_MUSIC_SEEK_PRESS);
    assert(input.param == 420U);

    service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_SEEK_DRAG, 500U);
    service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_SEEK_DRAG, 510U);
    assert(service_gui_input_consume(&input) == SERVICE_OK);
    assert(input.command == SERVICE_GUI_INPUT_MUSIC_SEEK_DRAG);
    assert(input.param == 510U);
}

/**
 * @brief 只丢掉 Queue 选曲，不误清进度条松手。
 */
static void test_drop_queue_select_keeps_seek(void)
{
    Service_GUI_InputTypeDef input;

    service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_SEEK_RELEASE, 120U);
    service_gui_input_drop_queue_select();
    assert(service_gui_input_consume(&input) == SERVICE_OK);
    assert(input.command == SERVICE_GUI_INPUT_MUSIC_SEEK_RELEASE);
    assert(input.param == 120U);
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
 * @brief has_pending 在 post 后为真，Consume 取走后为假（GUI 睡眠据此只取下限，#190）。
 */
static void test_has_pending_follows_post_and_consume(void)
{
    Service_GUI_InputTypeDef input;

    assert(!service_gui_input_has_pending());
    service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_NEXT, 0U);
    assert(service_gui_input_has_pending());
    assert(service_gui_input_consume(&input) == SERVICE_OK);
    assert(!service_gui_input_has_pending());

    service_gui_input_post(SERVICE_GUI_INPUT_MUSIC_QUEUE_SELECT, 2U);
    service_gui_input_drop_queue_select();
    assert(!service_gui_input_has_pending());
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
    test_consume_takes_seek_release_once();
    test_drag_after_pending_press_keeps_press_with_latest_value();
    test_drop_queue_select_keeps_seek();
    test_drop_queue_select_keeps_other_commands();
    test_has_pending_follows_post_and_consume();

    puts("gui_service_input_tests: all tests passed");
    return 0;
}
