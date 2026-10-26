/**
 ******************************************************************************
 * @file    test_gui_music_title.c
 * @brief   GUI 音乐分区曲名客户的主机行为测试。
 ******************************************************************************
 */

#include <assert.h>
#include <stddef.h>
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
 * @brief 冷启动：代次 0 请求、应答代次 G 后快照代次 0 → G 且位置不变，名字保留、不重请求。
 */
static void test_cold_start_keeps_name_when_reply_generation_matches(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef opening = identity_of(true, 0U, 0U);
    const GUI_MusicTitleIdentityTypeDef confirmed = identity_of(true, 5U, 0U);

    request_for(&client, &opening);
    assert(gui_music_title_request_identity(&client).generation == 0U);
    assert(gui_music_title_poll(&client, &opening, GUI_MUSIC_TITLE_READY, 5U) ==
           GUI_MUSIC_TITLE_ACTION_ACCEPT);
    assert(gui_music_title_poll(&client, &confirmed, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_NAME);
    assert(gui_music_title_poll(&client, &confirmed, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
}

/**
 * @brief 冷启动：已显示的名字应答代次不是 G，快照代次 0 → G 时清空并按 G 重请求。
 */
static void test_cold_start_rerequests_when_reply_generation_differs(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef opening = identity_of(true, 0U, 0U);
    const GUI_MusicTitleIdentityTypeDef confirmed = identity_of(true, 6U, 0U);
    GUI_MusicTitleIdentityTypeDef requested;

    request_for(&client, &opening);
    (void)gui_music_title_poll(&client, &opening, GUI_MUSIC_TITLE_READY, 5U);
    assert(gui_music_title_poll(&client, &confirmed, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_REQUEST);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_BLANK);
    requested = gui_music_title_request_identity(&client);
    assert(requested.generation == 6U);
    assert(requested.index == 0U);
}

/**
 * @brief 冷启动：代次 0 的请求在途时快照已确认为 G，应答代次为 G 就采信，不重请求。
 */
static void test_cold_start_accepts_in_flight_reply_for_confirmed_generation(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef opening = identity_of(true, 0U, 2U);
    const GUI_MusicTitleIdentityTypeDef confirmed = identity_of(true, 5U, 2U);

    request_for(&client, &opening);
    assert(gui_music_title_poll(&client, &confirmed, GUI_MUSIC_TITLE_PENDING, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_BLANK);
    assert(gui_music_title_poll(&client, &confirmed, GUI_MUSIC_TITLE_READY, 5U) ==
           GUI_MUSIC_TITLE_ACTION_ACCEPT);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_NAME);
    assert(gui_music_title_poll(&client, &confirmed, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
}

/**
 * @brief 冷启动：代次 0 的请求在途时快照已确认为 G，应答代次不是 G（含取不到）就丢弃并按 G 重请求。
 */
static void test_cold_start_discards_in_flight_reply_for_other_generation(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef opening = identity_of(true, 0U, 2U);
    const GUI_MusicTitleIdentityTypeDef confirmed = identity_of(true, 5U, 2U);
    const uint32_t replies[] = {4U, 0U};
    size_t i;

    for (i = 0U; i < (sizeof(replies) / sizeof(replies[0])); i++)
    {
        request_for(&client, &opening);
        (void)gui_music_title_poll(&client, &confirmed, GUI_MUSIC_TITLE_PENDING, 0U);
        assert(gui_music_title_poll(&client, &confirmed, GUI_MUSIC_TITLE_READY, replies[i]) ==
               GUI_MUSIC_TITLE_ACTION_DISCARD);
        assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_BLANK);
        assert(gui_music_title_poll(&client, &confirmed, GUI_MUSIC_TITLE_IDLE, 0U) ==
               GUI_MUSIC_TITLE_ACTION_REQUEST);
        assert(gui_music_title_request_identity(&client).generation == 5U);
    }
}

/**
 * @brief 连按 NEXT：槽忙期间身份连变三次，只等一次 READY 并丢弃，再按最后一首请求并采信。
 */
static void test_next_burst_requests_and_accepts_only_last(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef first = identity_of(true, 3U, 1U);
    const GUI_MusicTitleIdentityTypeDef second = identity_of(true, 3U, 2U);
    const GUI_MusicTitleIdentityTypeDef third = identity_of(true, 3U, 3U);
    const GUI_MusicTitleIdentityTypeDef last = identity_of(true, 3U, 4U);

    request_for(&client, &first);
    assert(gui_music_title_poll(&client, &second, GUI_MUSIC_TITLE_PENDING, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_poll(&client, &third, GUI_MUSIC_TITLE_PENDING, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_poll(&client, &last, GUI_MUSIC_TITLE_READY, 3U) ==
           GUI_MUSIC_TITLE_ACTION_DISCARD);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_BLANK);
    assert(gui_music_title_poll(&client, &last, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_REQUEST);
    assert(gui_music_title_request_identity(&client).index == 4U);
    assert(gui_music_title_poll(&client, &last, GUI_MUSIC_TITLE_PENDING, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_poll(&client, &last, GUI_MUSIC_TITLE_READY, 3U) ==
           GUI_MUSIC_TITLE_ACTION_ACCEPT);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_NAME);
}

/**
 * @brief 在途期间失去当前曲：立即显示 `--`，应答到达时丢弃且不再请求。
 */
static void test_losing_track_while_in_flight_discards_without_rerequest(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);
    const GUI_MusicTitleIdentityTypeDef none = identity_of(false, 0U, 0U);

    request_for(&client, &track);
    assert(gui_music_title_poll(&client, &none, GUI_MUSIC_TITLE_PENDING, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_NONE);
    assert(gui_music_title_poll(&client, &none, GUI_MUSIC_TITLE_READY, 3U) ==
           GUI_MUSIC_TITLE_ACTION_DISCARD);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_NONE);
    assert(gui_music_title_poll(&client, &none, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_NONE);
}

/**
 * @brief 坏文件跳过：显示名字时快照位置前进到候选，立即清空并按候选请求、采信候选名。
 */
static void test_bad_file_skip_requests_candidate(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef bad = identity_of(true, 3U, 5U);
    const GUI_MusicTitleIdentityTypeDef candidate = identity_of(true, 3U, 6U);

    request_for(&client, &bad);
    (void)gui_music_title_poll(&client, &bad, GUI_MUSIC_TITLE_READY, 3U);
    assert(gui_music_title_poll(&client, &candidate, GUI_MUSIC_TITLE_IDLE, 0U) ==
           GUI_MUSIC_TITLE_ACTION_REQUEST);
    assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_BLANK);
    assert(gui_music_title_request_identity(&client).index == 6U);
    assert(gui_music_title_poll(&client, &candidate, GUI_MUSIC_TITLE_READY, 3U) ==
           GUI_MUSIC_TITLE_ACTION_ACCEPT);
}

/**
 * @brief 回显未追上时归约给出的身份冻结：客户收到同一身份，名字保持、不发新请求。
 */
static void test_frozen_identity_keeps_name_without_request(void)
{
    GUI_MusicTitleClientTypeDef client;
    const GUI_MusicTitleIdentityTypeDef frozen = identity_of(true, 3U, 7U);
    int loop;

    request_for(&client, &frozen);
    (void)gui_music_title_poll(&client, &frozen, GUI_MUSIC_TITLE_READY, 3U);
    for (loop = 0; loop < 5; loop++)
    {
        assert(gui_music_title_poll(&client, &frozen, GUI_MUSIC_TITLE_IDLE, 0U) ==
               GUI_MUSIC_TITLE_ACTION_NONE);
        assert(gui_music_title_shown(&client) == GUI_MUSIC_TITLE_SHOW_NAME);
    }
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
    test_cold_start_keeps_name_when_reply_generation_matches();
    test_cold_start_rerequests_when_reply_generation_differs();
    test_cold_start_accepts_in_flight_reply_for_confirmed_generation();
    test_cold_start_discards_in_flight_reply_for_other_generation();
    test_next_burst_requests_and_accepts_only_last();
    test_losing_track_while_in_flight_discards_without_rerequest();
    test_bad_file_skip_requests_candidate();
    test_frozen_identity_keeps_name_without_request();
    printf("gui_music_title tests passed\n");
    return 0;
}
