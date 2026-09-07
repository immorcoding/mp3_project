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
    Gui_MusicTransportTypeDef transport;

    gui_music_transport_init(&transport);
    assert(gui_music_transport_is_playing(&transport) == false);
}

/**
 * @brief 无当前曲时按播放仍 paused，且报告未变化。
 */
static void test_toggle_without_current_stays_paused(void)
{
    Gui_MusicTransportTypeDef transport;
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
    Gui_MusicTransportTypeDef transport;

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
    Gui_MusicTransportTypeDef transport;

    gui_music_transport_init(&transport);
    assert(gui_music_transport_force_paused(&transport) == false);
    assert(gui_music_transport_is_playing(&transport) == false);
}

/**
 * @brief playing 时强制 paused 并报告变化。
 */
static void test_force_paused_stops_playing(void)
{
    Gui_MusicTransportTypeDef transport;

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
    gui_music_transport_init(NULL);
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

    puts("gui_music_transport_tests: all tests passed");
    return 0;
}
