/**
 ******************************************************************************
 * @file    test_gui_task_queue_window.c
 * @brief   GUI Task 窗口状态机的主机行为测试。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "APP/tasks/gui/gui_task_queue_window.h"

/**
 * @brief 未挂载时不 request，也不清空本来就没有的 Queue。
 */
static void test_does_not_request_before_sd_ready(void)
{
    Gui_QueueWindowClientTypeDef client;
    Gui_QueueWindowActionTypeDef action;

    gui_task_queue_window_client_init(&client);
    action = gui_task_queue_window_poll(
        &client,
        false,
        false,
        GUI_TASK_QUEUE_WINDOW_IDLE);
    assert(action == GUI_TASK_QUEUE_WINDOW_ACTION_NONE);
}

/**
 * @brief SD 就绪且槽位 IDLE 时要第一窗。
 */
static void test_requests_first_window_when_sd_ready_and_idle(void)
{
    Gui_QueueWindowClientTypeDef client;
    Gui_QueueWindowActionTypeDef action;

    gui_task_queue_window_client_init(&client);
    action = gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_IDLE);
    assert(action == GUI_TASK_QUEUE_WINDOW_ACTION_REQUEST);
}

/**
 * @brief PENDING 期间只等待，不重复 request。
 */
static void test_waits_while_pending(void)
{
    Gui_QueueWindowClientTypeDef client;
    Gui_QueueWindowActionTypeDef action;

    gui_task_queue_window_client_init(&client);
    (void)gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_IDLE);
    action = gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_PENDING);
    assert(action == GUI_TASK_QUEUE_WINDOW_ACTION_NONE);
}

/**
 * @brief READY 时整窗消费。
 */
static void test_applies_ready_window(void)
{
    Gui_QueueWindowClientTypeDef client;
    Gui_QueueWindowActionTypeDef action;

    gui_task_queue_window_client_init(&client);
    (void)gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_IDLE);
    (void)gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_PENDING);
    action = gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_READY);
    assert(action == GUI_TASK_QUEUE_WINDOW_ACTION_APPLY);
}

/**
 * @brief 消费成功后写回 IDLE 不得立刻再 request。
 */
static void test_does_not_rerequest_after_apply(void)
{
    Gui_QueueWindowClientTypeDef client;
    Gui_QueueWindowActionTypeDef action;

    gui_task_queue_window_client_init(&client);
    (void)gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_IDLE);
    (void)gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_READY);
    gui_task_queue_window_mark_applied(&client);
    action = gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_IDLE);
    assert(action == GUI_TASK_QUEUE_WINDOW_ACTION_NONE);
}

/**
 * @brief Apply 失败未 mark 时，写回 IDLE 后继续 request。
 */
static void test_retries_request_if_apply_not_marked(void)
{
    Gui_QueueWindowClientTypeDef client;
    Gui_QueueWindowActionTypeDef action;

    gui_task_queue_window_client_init(&client);
    (void)gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_READY);
    action = gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_IDLE);
    assert(action == GUI_TASK_QUEUE_WINDOW_ACTION_REQUEST);
}

/**
 * @brief request 失败槽位仍 IDLE 时，下一轮继续 request。
 */
static void test_retries_request_if_still_idle(void)
{
    Gui_QueueWindowClientTypeDef client;
    Gui_QueueWindowActionTypeDef action;

    gui_task_queue_window_client_init(&client);
    (void)gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_IDLE);
    action = gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_IDLE);
    assert(action == GUI_TASK_QUEUE_WINDOW_ACTION_REQUEST);
}

/**
 * @brief 已展示窗口后卷卸载，清成空窗。
 */
static void test_clears_displayed_window_when_sd_removed(void)
{
    Gui_QueueWindowClientTypeDef client;
    Gui_QueueWindowActionTypeDef action;

    gui_task_queue_window_client_init(&client);
    (void)gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_READY);
    gui_task_queue_window_mark_applied(&client);
    action = gui_task_queue_window_poll(
        &client,
        false,
        false,
        GUI_TASK_QUEUE_WINDOW_IDLE);
    assert(action == GUI_TASK_QUEUE_WINDOW_ACTION_CLEAR);
}

/**
 * @brief 消抖离开就绪但卷仍挂载时，不清空已展示窗口。
 */
static void test_does_not_clear_during_debounce_while_mounted(void)
{
    Gui_QueueWindowClientTypeDef client;
    Gui_QueueWindowActionTypeDef action;

    gui_task_queue_window_client_init(&client);
    (void)gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_READY);
    gui_task_queue_window_mark_applied(&client);
    action = gui_task_queue_window_poll(
        &client,
        false,
        true,
        GUI_TASK_QUEUE_WINDOW_IDLE);
    assert(action == GUI_TASK_QUEUE_WINDOW_ACTION_NONE);
}

/**
 * @brief 拔卡后空窗只清一次。
 */
static void test_does_not_clear_twice_while_sd_absent(void)
{
    Gui_QueueWindowClientTypeDef client;
    Gui_QueueWindowActionTypeDef action;

    gui_task_queue_window_client_init(&client);
    (void)gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_READY);
    gui_task_queue_window_mark_applied(&client);
    (void)gui_task_queue_window_poll(
        &client,
        false,
        false,
        GUI_TASK_QUEUE_WINDOW_IDLE);
    action = gui_task_queue_window_poll(
        &client,
        false,
        false,
        GUI_TASK_QUEUE_WINDOW_IDLE);
    assert(action == GUI_TASK_QUEUE_WINDOW_ACTION_NONE);
}

/**
 * @brief 重插卡后重新要窗。
 */
static void test_requests_again_after_sd_reinserted(void)
{
    Gui_QueueWindowClientTypeDef client;
    Gui_QueueWindowActionTypeDef action;

    gui_task_queue_window_client_init(&client);
    (void)gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_READY);
    gui_task_queue_window_mark_applied(&client);
    (void)gui_task_queue_window_poll(
        &client,
        false,
        false,
        GUI_TASK_QUEUE_WINDOW_IDLE);
    action = gui_task_queue_window_poll(
        &client,
        true,
        true,
        GUI_TASK_QUEUE_WINDOW_IDLE);
    assert(action == GUI_TASK_QUEUE_WINDOW_ACTION_REQUEST);
}

/**
 * @brief 卸载时若槽位已是 READY，先 APPLY 消费，避免停在 READY。
 */
static void test_applies_ready_before_clear_when_sd_removed(void)
{
    Gui_QueueWindowClientTypeDef client;
    Gui_QueueWindowActionTypeDef action;

    gui_task_queue_window_client_init(&client);
    action = gui_task_queue_window_poll(
        &client,
        false,
        false,
        GUI_TASK_QUEUE_WINDOW_READY);
    assert(action == GUI_TASK_QUEUE_WINDOW_ACTION_APPLY);
}

/**
 * @brief 运行全部窗口状态机测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_does_not_request_before_sd_ready();
    test_requests_first_window_when_sd_ready_and_idle();
    test_waits_while_pending();
    test_applies_ready_window();
    test_does_not_rerequest_after_apply();
    test_retries_request_if_apply_not_marked();
    test_retries_request_if_still_idle();
    test_clears_displayed_window_when_sd_removed();
    test_does_not_clear_during_debounce_while_mounted();
    test_does_not_clear_twice_while_sd_absent();
    test_requests_again_after_sd_reinserted();
    test_applies_ready_before_clear_when_sd_removed();

    puts("gui_task_queue_window_tests: all tests passed");
    return 0;
}
