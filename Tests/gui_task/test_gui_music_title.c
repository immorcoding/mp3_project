/**
 ******************************************************************************
 * @file    test_gui_music_title.c
 * @brief   GUI 音乐分区曲名客户的主机行为测试。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "APP/tasks/gui/music/gui_music_title.h"

/**
 * @brief 构造一个曲目身份。
 * @param[in] has_track 是否有当前曲。
 * @param[in] generation 曲库代次。
 * @param[in] index 播放列表位置。
 * @return 曲目身份。
 */
static GUI_MusicTitleIdentityTypeDef identity_of(bool has_track, uint32_t generation, uint16_t index)
{
    GUI_MusicTitleIdentityTypeDef identity;

    identity.has_track = has_track;
    identity.generation = generation;
    identity.index = index;
    return identity;
}

/**
 * @brief 无当前曲：不请求，显示 `--`。
 */
static void test_no_track_shows_none_without_request(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef none = identity_of(false, 0U, 0U);

    gui_music_title_client_init(&client);
    assert(gui_music_title_poll(&client, &none, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_NONE);
    assert(gui_music_title_poll(&client, &none, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
}

/**
 * @brief 有当前曲：留空并按该身份请求。
 */
static void test_track_shows_blank_and_requests_its_identity(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);
    GUI_MusicTitleIdentityTypeDef requested;

    gui_music_title_client_init(&client);
    assert(gui_music_title_poll(&client, &track, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_REQUEST);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_BLANK);
    requested = gui_music_title_request_identity(&client);
    assert(requested.generation == 3U);
    assert(requested.index == 7U);
}

/**
 * @brief 把客户推进到“已为 track 请求、槽 PENDING”的状态。
 * @param[out] client 曲名客户。
 * @param[in] track 当前身份。
 */
static void request_for(GUI_MusicTitleClientTypeDef *client, const GUI_MusicTitleIdentityTypeDef *track)
{
    gui_music_title_client_init(client);
    assert(gui_music_title_poll(client, track, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_REQUEST);
}

/**
 * @brief 槽 PENDING 时只等待、不重复请求，仍留空。
 */
static void test_waits_while_pending(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);

    request_for(&client, &track);
    assert(gui_music_title_poll(&client, &track, GUI_MUSIC_TITLE_PENDING, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_BLANK);
}

/**
 * @brief 同一身份的有效应答被采信为名字；之后槽 IDLE 不再请求。
 */
static void test_accepts_reply_for_same_identity(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);

    request_for(&client, &track);
    assert(gui_music_title_poll(&client, &track, GUI_MUSIC_TITLE_READY, 3U) ==
           GUI_MUSIC_TITLE_ACTION_ACCEPT);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_NAME);
    assert(gui_music_title_poll(&client, &track, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_NAME);
}

/**
 * @brief 应答代次 0 且身份未变：显示 `--`，丢弃应答且不重请求。
 */
static void test_generation_zero_reply_shows_none(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);

    request_for(&client, &track);
    assert(gui_music_title_poll(&client, &track, GUI_MUSIC_TITLE_READY, 0U) ==
           GUI_MUSIC_TITLE_ACTION_DISCARD);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_NONE);
    assert(gui_music_title_poll(&client, &track, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
}

/**
 * @brief 显示名字后身份变化：立即清空为空白，并按新身份请求。
 */
static void test_identity_change_clears_name_and_requests_new(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef first = identity_of(true, 3U, 7U);
    const GUI_MusicTitleIdentityTypeDef second = identity_of(true, 3U, 8U);
    GUI_MusicTitleIdentityTypeDef requested;

    request_for(&client, &first);
    (void)gui_music_title_poll(&client, &first, GUI_MUSIC_TITLE_READY, 3U);
    assert(gui_music_title_poll(&client, &second, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_REQUEST);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_BLANK);
    requested = gui_music_title_request_identity(&client);
    assert(requested.index == 8U);
}

/**
 * @brief 在途期间身份变化：应答到达时丢弃、不显示旧名，下一圈按新身份请求并采信。
 */
static void test_reply_for_old_identity_is_discarded_then_rerequested(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef first = identity_of(true, 3U, 7U);
    const GUI_MusicTitleIdentityTypeDef second = identity_of(true, 3U, 8U);

    request_for(&client, &first);
    assert(gui_music_title_poll(&client, &second, GUI_MUSIC_TITLE_PENDING, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_poll(&client, &second, GUI_MUSIC_TITLE_READY, 3U) ==
           GUI_MUSIC_TITLE_ACTION_DISCARD);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_BLANK);
    assert(gui_music_title_poll(&client, &second, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_REQUEST);
    assert(gui_music_title_request_identity(&client).index == 8U);
    assert(gui_music_title_poll(&client, &second, GUI_MUSIC_TITLE_READY, 3U) ==
           GUI_MUSIC_TITLE_ACTION_ACCEPT);
}

/**
 * @brief 显示名字后变为无当前曲：显示 `--`，不请求。
 */
static void test_losing_track_shows_none(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);
    const GUI_MusicTitleIdentityTypeDef none = identity_of(false, 0U, 0U);

    request_for(&client, &track);
    (void)gui_music_title_poll(&client, &track, GUI_MUSIC_TITLE_READY, 3U);
    assert(gui_music_title_poll(&client, &none, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_NONE);
}

/**
 * @brief 槽拒绝 request 后，下一圈按同一身份重试。
 */
static void test_retries_after_request_rejected(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);

    request_for(&client, &track);
    gui_music_title_request_failed(&client);
    assert(gui_music_title_poll(&client, &track, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_REQUEST);
}

/**
 * @brief 主机测试入口。
 * @return 0 表示全部通过。
 */
int main(void)
{
    test_no_track_shows_none_without_request();
    test_track_shows_blank_and_requests_its_identity();
    test_waits_while_pending();
    test_accepts_reply_for_same_identity();
    test_generation_zero_reply_shows_none();
    test_identity_change_clears_name_and_requests_new();
    test_reply_for_old_identity_is_discarded_then_rerequested();
    test_losing_track_shows_none();
    test_retries_after_request_rejected();
    printf("gui_music_title tests passed\n");
    return 0;
}
