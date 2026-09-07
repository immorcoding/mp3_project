/**
 ******************************************************************************
 * @file    test_gui_music_transport.c
 * @brief   音乐播放/暂停策略的主机行为测试。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "APP/tasks/gui/music/gui_music_transport.h"

/**
 * @brief 上电为 paused。
 */
static void test_starts_paused(void)
{
    GUI_MusicTransportTypeDef transport;

    gui_music_transport_init(&transport);
    assert(gui_music_transport_is_playing(&transport) == false);
}

/**
 * @brief 无当前曲时按播放仍 paused，且报告未变化。
 */
static void test_toggle_without_current_stays_paused(void)
{
    GUI_MusicTransportTypeDef transport;
    bool changed;

    gui_music_transport_init(&transport);
    changed = gui_music_transport_toggle(&transport, false);
    assert(changed == false);
    assert(gui_music_transport_is_playing(&transport) == false);
}

/**
 * @brief 有当前曲时在 paused 与 playing 之间翻转。
 */
static void test_toggle_with_current_flips_playing(void)
{
    GUI_MusicTransportTypeDef transport;

    gui_music_transport_init(&transport);
    assert(gui_music_transport_toggle(&transport, true) == true);
    assert(gui_music_transport_is_playing(&transport) == true);
    assert(gui_music_transport_toggle(&transport, true) == true);
    assert(gui_music_transport_is_playing(&transport) == false);
}

/**
 * @brief 已 paused 时再强制 paused 不报告变化。
 */
static void test_force_paused_when_already_paused_is_unchanged(void)
{
    GUI_MusicTransportTypeDef transport;

    gui_music_transport_init(&transport);
    assert(gui_music_transport_force_paused(&transport) == false);
    assert(gui_music_transport_is_playing(&transport) == false);
}

/**
 * @brief playing 时强制 paused 并报告变化。
 */
static void test_force_paused_stops_playing(void)
{
    GUI_MusicTransportTypeDef transport;

    gui_music_transport_init(&transport);
    (void)gui_music_transport_toggle(&transport, true);
    assert(gui_music_transport_force_paused(&transport) == true);
    assert(gui_music_transport_is_playing(&transport) == false);
}

/**
 * @brief 指针为空时视为未变化且不在播。
 */
static void test_null_handle_is_safe(void)
{
    assert(gui_music_transport_toggle(NULL, true) == false);
    assert(gui_music_transport_force_paused(NULL) == false);
    assert(gui_music_transport_is_playing(NULL) == false);
    assert(gui_music_transport_set_progress(NULL, 10U) == false);
    assert(gui_music_transport_reset_progress(NULL) == false);
    assert(gui_music_transport_get_progress(NULL) == 0U);
    gui_music_transport_init(NULL);
}

/**
 * @brief 上电假进度为 0。
 */
static void test_starts_at_zero_progress(void)
{
    GUI_MusicTransportTypeDef transport;

    gui_music_transport_init(&transport);
    assert(gui_music_transport_get_progress(&transport) == 0U);
}

/**
 * @brief 记下百分比并报告变化；同值再写则不变。
 */
static void test_set_progress_reports_change(void)
{
    GUI_MusicTransportTypeDef transport;

    gui_music_transport_init(&transport);
    assert(gui_music_transport_set_progress(&transport, 40U) == true);
    assert(gui_music_transport_get_progress(&transport) == 40U);
    assert(gui_music_transport_set_progress(&transport, 40U) == false);
    assert(gui_music_transport_is_playing(&transport) == false);
}

/**
 * @brief 超过 100 的百分比被夹到 100。
 */
static void test_set_progress_clamps_to_hundred(void)
{
    GUI_MusicTransportTypeDef transport;

    gui_music_transport_init(&transport);
    assert(gui_music_transport_set_progress(&transport, 255U) == true);
    assert(gui_music_transport_get_progress(&transport) == 100U);
}

/**
 * @brief 复位进度为 0。
 */
static void test_reset_progress_clears_percent(void)
{
    GUI_MusicTransportTypeDef transport;

    gui_music_transport_init(&transport);
    (void)gui_music_transport_set_progress(&transport, 40U);
    assert(gui_music_transport_reset_progress(&transport) == true);
    assert(gui_music_transport_get_progress(&transport) == 0U);
    assert(gui_music_transport_reset_progress(&transport) == false);
}

/**
 * @brief 强制 paused 不改进度。
 */
static void test_force_paused_keeps_progress(void)
{
    GUI_MusicTransportTypeDef transport;

    gui_music_transport_init(&transport);
    (void)gui_music_transport_toggle(&transport, true);
    (void)gui_music_transport_set_progress(&transport, 25U);
    assert(gui_music_transport_force_paused(&transport) == true);
    assert(gui_music_transport_get_progress(&transport) == 25U);
}

/**
 * @brief 运行全部播放/暂停策略测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_starts_paused();
    test_toggle_without_current_stays_paused();
    test_toggle_with_current_flips_playing();
    test_force_paused_when_already_paused_is_unchanged();
    test_force_paused_stops_playing();
    test_null_handle_is_safe();
    test_starts_at_zero_progress();
    test_set_progress_reports_change();
    test_set_progress_clamps_to_hundred();
    test_reset_progress_clears_percent();
    test_force_paused_keeps_progress();

    puts("gui_music_transport_tests: all tests passed");
    return 0;
}
