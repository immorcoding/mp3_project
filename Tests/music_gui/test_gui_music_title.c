/**
 ******************************************************************************
 * @file    test_gui_music_title.c
 * @brief   GUI 音乐分区曲名客户的主机行为测试（GUI-26、STOR-15、#193、#218）：按格子改写。
 *
 * @details
 *          曲名客户不再有名称槽的三态：GUI 把最新需求写进需求格（带递增序号），前台任务答在结果格，结果带回所答的需求序号，
 *          客户只采信序号等于最新需求的结果（曲名要比序号，窗口不比，#200）。本测试扮演 GUI 任务和前台任务：
 *          每帧调 `gui_music_title_poll()`，到点就“写需求”（`request_due` → `request` → `request_sent`），再按最近一次写出的需求序号
 *          构造结果。期望值写成字面量；只经 `gui_music_title.h` 的公开函数观察。主机测试是单线程的，不证明真实并发。
 ******************************************************************************
 */

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "APP/apps/music/gui/gui_music_title.h"

/** @brief 测试台：曲名客户加“最近一次写出的需求”与写出次数。 */
typedef struct
{
    GUI_MusicTitleClientTypeDef client;
    MusicCells_TitleRequestTypeDef last_request; /**< 最近一次写进需求格的内容。 */
    uint32_t writes;                             /**< 写出的需求次数。 */
    bool wrote_this_frame;                       /**< 上一帧是否写出了需求。 */
} Rig;

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
 * @brief 构造一份曲名结果。
 * @param[in] sequence 所答的需求序号。
 * @param[in] generation 实际代次；0 表示取不到。
 * @param[in] title 曲名。
 * @param[in] artist 歌手。
 * @return 结果。
 */
static MusicCells_TitleResultTypeDef result_of(uint32_t sequence, uint32_t generation, const char *title, const char *artist)
{
    MusicCells_TitleResultTypeDef result;

    (void)memset(&result, 0, sizeof(result));
    result.RequestSequence = sequence;
    result.Generation = generation;
    (void)strncpy(result.Title, (title != NULL) ? title : "", sizeof(result.Title) - 1U);
    (void)strncpy(result.Artist, (artist != NULL) ? artist : "", sizeof(result.Artist) - 1U);
    return result;
}

/**
 * @brief 推进一帧：poll，然后像 gui_music.c 那样到点写需求。
 * @param[in,out] rig 测试台。
 * @param[in] identity 本帧已追上的曲目身份。
 * @param[in] reply 本帧读到的新结果；没有为 NULL。
 * @param[in] write_succeeds 写需求格是否成功；为假模拟拿不到锁，下一帧重试。
 * @return 本帧对结果的动作。
 */
static GUI_MusicTitleActionTypeDef frame_with_write(Rig *rig,
                                                    const GUI_MusicTitleIdentityTypeDef *identity,
                                                    const MusicCells_TitleResultTypeDef *reply,
                                                    bool write_succeeds)
{
    const GUI_MusicTitleActionTypeDef action = gui_music_title_poll(&rig->client, identity, reply);

    rig->wrote_this_frame = false;
    if (gui_music_title_request_due(&rig->client))
    {
        const MusicCells_TitleRequestTypeDef request = gui_music_title_request(&rig->client);

        if (write_succeeds)
        {
            rig->last_request = request;
            rig->writes++;
            rig->wrote_this_frame = true;
            gui_music_title_request_sent(&rig->client);
        }
    }
    return action;
}

/**
 * @brief 推进一帧（写需求总是成功）。
 * @param[in,out] rig 测试台。
 * @param[in] identity 本帧已追上的曲目身份。
 * @param[in] reply 本帧读到的新结果；没有为 NULL。
 * @return 本帧对结果的动作。
 */
static GUI_MusicTitleActionTypeDef frame(Rig *rig, const GUI_MusicTitleIdentityTypeDef *identity, const MusicCells_TitleResultTypeDef *reply)
{
    return frame_with_write(rig, identity, reply, true);
}

/**
 * @brief 前台任务答最近一次写出的需求：返回带回该需求序号的结果。
 * @param[in] rig 测试台。
 * @param[in] generation 实际代次；0 表示取不到。
 * @return 结果。
 */
static MusicCells_TitleResultTypeDef answer(const Rig *rig, uint32_t generation)
{
    return result_of(rig->last_request.Sequence, generation, "Title", "Artist");
}

/**
 * @brief 开始一个用例：客户复位。
 * @param[out] rig 测试台。
 */
static void rig_init(Rig *rig)
{
    (void)memset(rig, 0, sizeof(*rig));
    gui_music_title_client_init(&rig->client);
}

/**
 * @brief 开始一个用例：客户复位，并为 track 写出第一条需求。
 * @param[out] rig 测试台。
 * @param[in] track 当前身份。
 */
static void rig_requested_for(Rig *rig, const GUI_MusicTitleIdentityTypeDef *track)
{
    rig_init(rig);
    assert(frame(rig, track, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(rig->wrote_this_frame);
}

/** @brief 无当前曲：不写需求，显示 `--`。 */
static void test_no_track_shows_none_without_request(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef none = identity_of(false, 0U, 0U);

    rig_init(&rig);
    assert(frame(&rig, &none, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_NONE);
    assert(rig.writes == 0U);
    assert(frame(&rig, &none, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(rig.writes == 0U);
}

/** @brief 有当前曲：留空并按该身份写需求，需求序号从 1 起，要的是这一首。 */
static void test_track_shows_blank_and_writes_a_request_for_its_identity(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);

    rig_requested_for(&rig, &track);

    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_BLANK);
    assert(rig.last_request.Wanted);
    assert(rig.last_request.Sequence == 1U);
    assert(rig.last_request.Generation == 3U);
    assert(rig.last_request.Index == 7U);
}

/** @brief 需求写出后、结果没到之前只等：不重复写，仍留空。 */
static void test_waits_for_the_reply_without_writing_again(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);

    rig_requested_for(&rig, &track);
    assert(frame(&rig, &track, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(frame(&rig, &track, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(rig.writes == 1U);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_BLANK);
}

/** @brief 同一身份的有效结果被采信为名字；之后不再写需求。 */
static void test_accepts_the_reply_for_the_same_identity(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);
    MusicCells_TitleResultTypeDef reply;

    rig_requested_for(&rig, &track);
    reply = answer(&rig, 3U);
    assert(frame(&rig, &track, &reply) == GUI_MUSIC_TITLE_ACTION_ACCEPT);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_NAME);
    assert(frame(&rig, &track, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_NAME);
    assert(rig.writes == 1U);
}

/** @brief 结果代次 0 且身份未变：显示 `--`，丢弃结果，不重写需求。 */
static void test_a_generation_zero_reply_shows_none_without_rewriting(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);
    MusicCells_TitleResultTypeDef reply;

    rig_requested_for(&rig, &track);
    reply = answer(&rig, 0U);
    assert(frame(&rig, &track, &reply) == GUI_MUSIC_TITLE_ACTION_DISCARD);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_NONE);
    assert(frame(&rig, &track, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(rig.writes == 1U);
}

/** @brief 显示名字后身份变化：当帧清空为空白，并立刻按新身份写新序号的需求（不等旧结果）。 */
static void test_an_identity_change_clears_the_name_and_writes_a_newer_request(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef first = identity_of(true, 3U, 7U);
    const GUI_MusicTitleIdentityTypeDef second = identity_of(true, 3U, 8U);
    MusicCells_TitleResultTypeDef reply;

    rig_requested_for(&rig, &first);
    reply = answer(&rig, 3U);
    (void)frame(&rig, &first, &reply);

    assert(frame(&rig, &second, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_BLANK);
    assert(rig.wrote_this_frame);
    assert(rig.last_request.Index == 8U);
    assert(rig.last_request.Sequence == 2U);
}

/** @brief 在途期间身份变化：旧需求的结果序号落后，被丢弃、不显示旧名；新需求的结果被采信。 */
static void test_a_reply_to_an_overwritten_request_is_discarded_and_the_newer_one_is_accepted(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef first = identity_of(true, 3U, 7U);
    const GUI_MusicTitleIdentityTypeDef second = identity_of(true, 3U, 8U);
    MusicCells_TitleResultTypeDef old_reply;
    MusicCells_TitleResultTypeDef new_reply;

    rig_requested_for(&rig, &first);
    old_reply = answer(&rig, 3U);                     /* 第 1 号需求的结果。 */
    assert(frame(&rig, &second, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    new_reply = answer(&rig, 3U);                     /* 第 2 号需求的结果。 */

    assert(frame(&rig, &second, &old_reply) == GUI_MUSIC_TITLE_ACTION_DISCARD);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_BLANK);
    assert(frame(&rig, &second, &new_reply) == GUI_MUSIC_TITLE_ACTION_ACCEPT);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_NAME);
    assert(rig.writes == 2U);
}

/** @brief 显示名字后变为无当前曲：显示 `--`，不写需求。 */
static void test_losing_the_track_shows_none(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);
    const GUI_MusicTitleIdentityTypeDef none = identity_of(false, 0U, 0U);
    MusicCells_TitleResultTypeDef reply;

    rig_requested_for(&rig, &track);
    reply = answer(&rig, 3U);
    (void)frame(&rig, &track, &reply);

    assert(frame(&rig, &none, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_NONE);
    assert(rig.writes == 1U);
}

/** @brief 需求格拿不到锁（写失败）：下一帧按同一序号重试，直到写成功。 */
static void test_a_request_that_could_not_be_written_is_retried_on_the_next_frame(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);

    rig_init(&rig);
    assert(frame_with_write(&rig, &track, NULL, false) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(rig.writes == 0U);
    assert(gui_music_title_request_due(&rig.client));

    assert(frame_with_write(&rig, &track, NULL, false) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(rig.writes == 0U);

    assert(frame(&rig, &track, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(rig.writes == 1U);
    assert(rig.last_request.Sequence == 1U); /* 失败的尝试不占序号。 */
    assert(!gui_music_title_request_due(&rig.client));
}

/** @brief 冷启动：代次 0 的需求得到代次 G 的结果后，快照代次 0 → G 且位置不变，名字保留、不重写需求。 */
static void test_cold_start_keeps_the_name_when_the_reply_generation_matches(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef opening = identity_of(true, 0U, 0U);
    const GUI_MusicTitleIdentityTypeDef confirmed = identity_of(true, 5U, 0U);
    MusicCells_TitleResultTypeDef reply;

    rig_requested_for(&rig, &opening);
    assert(rig.last_request.Generation == 0U);
    reply = answer(&rig, 5U);
    assert(frame(&rig, &opening, &reply) == GUI_MUSIC_TITLE_ACTION_ACCEPT);
    assert(frame(&rig, &confirmed, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_NAME);
    assert(frame(&rig, &confirmed, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(rig.writes == 1U);
}

/** @brief 冷启动：已显示的名字所答代次不是 G，快照代次 0 → G 时清空并按 G 重写。 */
static void test_cold_start_rewrites_when_the_name_generation_differs(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef opening = identity_of(true, 0U, 0U);
    const GUI_MusicTitleIdentityTypeDef confirmed = identity_of(true, 6U, 0U);
    MusicCells_TitleResultTypeDef reply;

    rig_requested_for(&rig, &opening);
    reply = answer(&rig, 5U);
    (void)frame(&rig, &opening, &reply);

    assert(frame(&rig, &confirmed, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_BLANK);
    assert(rig.wrote_this_frame);
    assert(rig.last_request.Generation == 6U);
    assert(rig.last_request.Index == 0U);
}

/** @brief 冷启动：代次 0 的需求在途时快照已确认为 G：不重写；所答代次为 G 就采信。 */
static void test_cold_start_accepts_an_in_flight_reply_for_the_confirmed_generation(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef opening = identity_of(true, 0U, 2U);
    const GUI_MusicTitleIdentityTypeDef confirmed = identity_of(true, 5U, 2U);
    MusicCells_TitleResultTypeDef reply;

    rig_requested_for(&rig, &opening);
    assert(frame(&rig, &confirmed, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(!rig.wrote_this_frame);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_BLANK);

    reply = answer(&rig, 5U);
    assert(frame(&rig, &confirmed, &reply) == GUI_MUSIC_TITLE_ACTION_ACCEPT);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_NAME);
    assert(frame(&rig, &confirmed, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(rig.writes == 1U);
}

/** @brief 冷启动：代次 0 的需求在途时快照已确认为 G，所答代次不是 G（含取不到）：丢弃并按 G 重写。 */
static void test_cold_start_discards_an_in_flight_reply_for_another_generation_and_rewrites(void)
{
    const uint32_t reply_generations[] = {4U, 0U};

    for (size_t i = 0U; i < (sizeof(reply_generations) / sizeof(reply_generations[0])); i++)
    {
        Rig rig;
        const GUI_MusicTitleIdentityTypeDef opening = identity_of(true, 0U, 2U);
        const GUI_MusicTitleIdentityTypeDef confirmed = identity_of(true, 5U, 2U);
        MusicCells_TitleResultTypeDef reply;

        rig_requested_for(&rig, &opening);
        (void)frame(&rig, &confirmed, NULL);
        reply = answer(&rig, reply_generations[i]);

        assert(frame(&rig, &confirmed, &reply) == GUI_MUSIC_TITLE_ACTION_DISCARD);
        assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_BLANK);
        assert(rig.wrote_this_frame);
        assert(rig.last_request.Generation == 5U);
        assert(rig.last_request.Sequence == 2U);
    }
}

/** @brief 连按 NEXT：身份连变三次，每次写更新的需求；前三首的结果都因序号落后被丢弃，只采信最后一首。 */
static void test_next_burst_accepts_only_the_last_request(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef first = identity_of(true, 3U, 1U);
    const GUI_MusicTitleIdentityTypeDef second = identity_of(true, 3U, 2U);
    const GUI_MusicTitleIdentityTypeDef third = identity_of(true, 3U, 3U);
    const GUI_MusicTitleIdentityTypeDef last = identity_of(true, 3U, 4U);
    MusicCells_TitleResultTypeDef reply_1;
    MusicCells_TitleResultTypeDef reply_2;
    MusicCells_TitleResultTypeDef reply_3;
    MusicCells_TitleResultTypeDef reply_4;

    rig_requested_for(&rig, &first);
    reply_1 = answer(&rig, 3U);
    (void)frame(&rig, &second, NULL);
    reply_2 = answer(&rig, 3U);
    (void)frame(&rig, &third, NULL);
    reply_3 = answer(&rig, 3U);
    (void)frame(&rig, &last, NULL);
    reply_4 = answer(&rig, 3U);
    assert(rig.last_request.Index == 4U);
    assert(rig.last_request.Sequence == 4U);

    assert(frame(&rig, &last, &reply_1) == GUI_MUSIC_TITLE_ACTION_DISCARD);
    assert(frame(&rig, &last, &reply_2) == GUI_MUSIC_TITLE_ACTION_DISCARD);
    assert(frame(&rig, &last, &reply_3) == GUI_MUSIC_TITLE_ACTION_DISCARD);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_BLANK);
    assert(frame(&rig, &last, &reply_4) == GUI_MUSIC_TITLE_ACTION_ACCEPT);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_NAME);
}

/** @brief 在途期间失去当前曲：当帧显示 `--`，结果到达时丢弃，也不再写需求。 */
static void test_losing_the_track_while_a_request_is_in_flight_discards_without_a_new_request(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);
    const GUI_MusicTitleIdentityTypeDef none = identity_of(false, 0U, 0U);
    MusicCells_TitleResultTypeDef reply;

    rig_requested_for(&rig, &track);
    reply = answer(&rig, 3U);
    assert(frame(&rig, &none, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_NONE);
    assert(frame(&rig, &none, &reply) == GUI_MUSIC_TITLE_ACTION_DISCARD);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_NONE);
    assert(rig.writes == 1U);
}

/** @brief 坏文件跳过：显示名字时快照位置前进到候选，当帧清空并按候选写需求，采信候选名。 */
static void test_a_bad_file_skip_writes_a_request_for_the_candidate(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef bad = identity_of(true, 3U, 5U);
    const GUI_MusicTitleIdentityTypeDef candidate = identity_of(true, 3U, 6U);
    MusicCells_TitleResultTypeDef reply;

    rig_requested_for(&rig, &bad);
    reply = answer(&rig, 3U);
    (void)frame(&rig, &bad, &reply);

    assert(frame(&rig, &candidate, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_BLANK);
    assert(rig.last_request.Index == 6U);
    reply = answer(&rig, 3U);
    assert(frame(&rig, &candidate, &reply) == GUI_MUSIC_TITLE_ACTION_ACCEPT);
}

/** @brief 回显未追上时归约给出的身份冻结：客户收到同一身份，名字保持、不写新需求。 */
static void test_a_frozen_identity_keeps_the_name_without_writing(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef frozen = identity_of(true, 3U, 7U);
    MusicCells_TitleResultTypeDef reply;

    rig_requested_for(&rig, &frozen);
    reply = answer(&rig, 3U);
    (void)frame(&rig, &frozen, &reply);
    for (int loop = 0; loop < 5; loop++)
    {
        assert(frame(&rig, &frozen, NULL) == GUI_MUSIC_TITLE_ACTION_NONE);
        assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_NAME);
    }
    assert(rig.writes == 1U);
}

/** @brief 前台任务曲库发布后重答同一个需求（代次 0 的结果之后又来一份同序号的名字）：显示名字，不用 GUI 重写需求。 */
static void test_the_foreground_answering_the_same_request_again_after_the_catalog_is_published_shows_the_name(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);
    MusicCells_TitleResultTypeDef first_reply;
    MusicCells_TitleResultTypeDef second_reply;

    rig_requested_for(&rig, &track);
    first_reply = answer(&rig, 0U);
    second_reply = answer(&rig, 3U);
    assert(frame(&rig, &track, &first_reply) == GUI_MUSIC_TITLE_ACTION_DISCARD);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_NONE);

    /* 曲库发布后前台再答一次，序号相同、代次非 0：采信（曲名区由 `--` 变成名字）。 */
    assert(frame(&rig, &track, &second_reply) == GUI_MUSIC_TITLE_ACTION_ACCEPT);
    assert(gui_music_title_shown(&rig.client) == GUI_MUSIC_TITLE_SHOW_NAME);
    assert(rig.writes == 1U);
}

/** @brief 测试用结果里的两列：每个用例按需改写。 */
static const char *reply_title = "Clair de Lune";
static const char *reply_artist = "Claude Debussy";

/**
 * @brief 推进一帧并取本帧要 Apply 的曲名与歌手，与 gui_music.c 的编排一致。
 * @param[in,out] rig 测试台。
 * @param[in] identity 本帧已追上的曲目身份。
 * @param[in] reply_generation 本帧新结果的代次；负数表示没有新结果。
 * @param[out] action 本帧动作；可为 NULL。
 * @param[out] text 本帧要 Apply 的两列；返回 false 时不改。
 * @return 本帧是否需要 Apply。
 */
static bool step(Rig *rig,
                 const GUI_MusicTitleIdentityTypeDef *identity,
                 int64_t reply_generation,
                 GUI_MusicTitleActionTypeDef *action,
                 GUI_MusicTitleTextTypeDef *text)
{
    MusicCells_TitleResultTypeDef reply;
    GUI_MusicTitleActionTypeDef a;

    if (reply_generation >= 0)
    {
        reply = result_of(rig->last_request.Sequence, (uint32_t)reply_generation, reply_title, reply_artist);
        a = frame(rig, identity, &reply);
    }
    else
    {
        a = frame(rig, identity, NULL);
    }

    if (action != NULL)
    {
        *action = a;
    }
    return gui_music_title_text(&rig->client, a, reply_title, reply_artist, text);
}

/**
 * @brief 断言一次 Apply 的曲名与歌手。
 * @param[in] text 实际两列。
 * @param[in] title 期望曲名。
 * @param[in] artist 期望歌手。
 */
static void assert_text(const GUI_MusicTitleTextTypeDef *text, const char *title, const char *artist)
{
    assert(text->title != NULL);
    assert(text->artist != NULL);
    assert(strcmp(text->title, title) == 0);
    assert(strcmp(text->artist, artist) == 0);
}

/** @brief 同一身份的结果到达：曲名与歌手在同一次 Apply 里一起出现。 */
static void test_accept_applies_title_and_artist_together(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);
    GUI_MusicTitleTextTypeDef text;
    GUI_MusicTitleActionTypeDef action;

    rig_init(&rig);
    reply_title = "Clair de Lune";
    reply_artist = "Claude Debussy";
    assert(step(&rig, &track, -1, &action, &text)); /* 写需求的同一帧交出空白。 */
    assert_text(&text, "", "");
    assert(step(&rig, &track, 3, &action, &text));
    assert(action == GUI_MUSIC_TITLE_ACTION_ACCEPT);
    assert_text(&text, "Clair de Lune", "Claude Debussy");
    assert(!step(&rig, &track, -1, &action, &text));
}

/** @brief 结果里歌手为空串或 NULL：歌手位交出界面文案 `Unknown Artist`，与 Queue 一致（GUI-25、GUI-26）。 */
static void test_accept_without_artist_applies_unknown_artist(void)
{
    const char *const missing[] = {"", NULL};

    for (size_t i = 0U; i < (sizeof(missing) / sizeof(missing[0])); i++)
    {
        Rig rig;
        const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);
        GUI_MusicTitleTextTypeDef text;

        rig_init(&rig);
        reply_title = "Kiss the Rain (Live)";
        reply_artist = missing[i];
        (void)step(&rig, &track, -1, NULL, &text);
        assert(step(&rig, &track, 3, NULL, &text));
        assert_text(&text, "Kiss the Rain (Live)", "Unknown Artist");
    }
}

/** @brief 切歌：身份一变，曲名与歌手同时清空成一次 ("", "")，新结果到达后同时换成新歌的。 */
static void test_switch_clears_both_then_applies_the_new_pair(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef first = identity_of(true, 3U, 5U);
    const GUI_MusicTitleIdentityTypeDef second = identity_of(true, 3U, 6U);
    GUI_MusicTitleTextTypeDef text;
    GUI_MusicTitleActionTypeDef action;

    rig_init(&rig);
    reply_title = "Experience (Einaudi Live)";
    reply_artist = "Ludovico Einaudi";
    (void)step(&rig, &first, -1, NULL, &text);
    (void)step(&rig, &first, 3, NULL, &text);

    assert(step(&rig, &second, -1, &action, &text));
    assert(action == GUI_MUSIC_TITLE_ACTION_NONE);
    assert_text(&text, "", "");
    assert(rig.wrote_this_frame);
    assert(!step(&rig, &second, -1, NULL, &text));
    reply_title = "Merry Christmas Mr. Lawrence";
    reply_artist = "";
    assert(step(&rig, &second, 3, &action, &text));
    assert(action == GUI_MUSIC_TITLE_ACTION_ACCEPT);
    assert_text(&text, "Merry Christmas Mr. Lawrence", "Unknown Artist");
}

/** @brief 连按 NEXT：中间歌曲的结果被丢弃、不交出它的歌手；只有最后一首的两列出现。 */
static void test_next_burst_never_applies_an_intermediate_artist(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef first = identity_of(true, 3U, 1U);
    const GUI_MusicTitleIdentityTypeDef second = identity_of(true, 3U, 2U);
    const GUI_MusicTitleIdentityTypeDef last = identity_of(true, 3U, 4U);
    GUI_MusicTitleTextTypeDef text;
    GUI_MusicTitleActionTypeDef action;
    MusicCells_TitleResultTypeDef stale;

    rig_init(&rig);
    assert(step(&rig, &first, -1, NULL, &text));
    assert_text(&text, "", "");
    stale = result_of(rig.last_request.Sequence, 3U, "Gymnopedie No.1", "Erik Satie");
    assert(!step(&rig, &second, -1, NULL, &text));
    assert(!step(&rig, &last, -1, NULL, &text));

    action = frame(&rig, &last, &stale);
    assert(action == GUI_MUSIC_TITLE_ACTION_DISCARD);
    assert(!gui_music_title_text(&rig.client, action, stale.Title, stale.Artist, &text));

    reply_title = "Nuvole Bianche";
    reply_artist = "Ludovico Einaudi";
    assert(step(&rig, &last, 3, &action, &text));
    assert(action == GUI_MUSIC_TITLE_ACTION_ACCEPT);
    assert_text(&text, "Nuvole Bianche", "Ludovico Einaudi");
}

/** @brief 坏文件跳过：坏文件的两列清空，候选的曲名与歌手同一次出现。 */
static void test_a_bad_file_skip_applies_the_candidate_pair(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef bad = identity_of(true, 3U, 5U);
    const GUI_MusicTitleIdentityTypeDef candidate = identity_of(true, 3U, 6U);
    GUI_MusicTitleTextTypeDef text;

    rig_init(&rig);
    reply_title = "Broken";
    reply_artist = "Nobody";
    (void)step(&rig, &bad, -1, NULL, &text);
    (void)step(&rig, &bad, 3, NULL, &text);
    assert(step(&rig, &candidate, -1, NULL, &text));
    assert_text(&text, "", "");
    reply_title = "周杰伦 - 晴天";
    reply_artist = "周杰伦 / 五月天";
    assert(step(&rig, &candidate, 3, NULL, &text));
    assert_text(&text, "周杰伦 - 晴天", "周杰伦 / 五月天");
}

/** @brief 过期结果：在途期间身份变化，旧结果不交出任何文本；新结果到达才交出新的两列。 */
static void test_a_stale_reply_applies_nothing(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef first = identity_of(true, 3U, 7U);
    const GUI_MusicTitleIdentityTypeDef second = identity_of(true, 3U, 8U);
    GUI_MusicTitleTextTypeDef text;
    MusicCells_TitleResultTypeDef stale;
    GUI_MusicTitleActionTypeDef action;

    rig_init(&rig);
    (void)step(&rig, &first, -1, NULL, &text);
    stale = result_of(rig.last_request.Sequence, 3U, "Old Song", "Old Artist");
    assert(!step(&rig, &second, -1, NULL, &text));

    action = frame(&rig, &second, &stale);
    assert(action == GUI_MUSIC_TITLE_ACTION_DISCARD);
    assert(!gui_music_title_text(&rig.client, action, stale.Title, stale.Artist, &text));

    reply_title = "New Song";
    reply_artist = "New Artist";
    assert(step(&rig, &second, 3, NULL, &text));
    assert_text(&text, "New Song", "New Artist");
}

/** @brief 冷启动代次 0 → G 的同一首：名字与歌手都保留，不再交出清空。 */
static void test_cold_start_confirmation_keeps_the_pair(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef opening = identity_of(true, 0U, 0U);
    const GUI_MusicTitleIdentityTypeDef confirmed = identity_of(true, 5U, 0U);
    GUI_MusicTitleTextTypeDef text;

    rig_init(&rig);
    reply_title = "Clair de Lune";
    reply_artist = "Claude Debussy";
    (void)step(&rig, &opening, -1, NULL, &text);
    assert(step(&rig, &opening, 5, NULL, &text));
    assert_text(&text, "Clair de Lune", "Claude Debussy");
    assert(!step(&rig, &confirmed, -1, NULL, &text));
    assert(!step(&rig, &confirmed, -1, NULL, &text));
}

/** @brief 无当前曲、拔卡、空库（失去当前曲）与取不到名字：曲名 `--`、歌手为空串，不显示 `Unknown Artist`。 */
static void test_none_states_apply_dash_and_an_empty_artist(void)
{
    Rig rig;
    const GUI_MusicTitleIdentityTypeDef none = identity_of(false, 0U, 0U);
    const GUI_MusicTitleIdentityTypeDef track = identity_of(true, 3U, 7U);
    GUI_MusicTitleTextTypeDef text;

    /* 冷启动无当前曲。 */
    rig_init(&rig);
    assert(step(&rig, &none, -1, NULL, &text));
    assert_text(&text, "--", "");
    assert(!step(&rig, &none, -1, NULL, &text));

    /* 有名字与歌手后拔卡或换成空库：两列一起换成 `--` 与空串。 */
    reply_title = "Clair de Lune";
    reply_artist = "Claude Debussy";
    (void)step(&rig, &track, -1, NULL, &text);
    (void)step(&rig, &track, 3, NULL, &text);
    assert(step(&rig, &none, -1, NULL, &text));
    assert_text(&text, "--", "");

    /* 有当前曲但结果代次 0（取不到名字）：`--`，歌手为空串。 */
    rig_init(&rig);
    (void)step(&rig, &track, -1, NULL, &text);
    assert(step(&rig, &track, 0, NULL, &text));
    assert_text(&text, "--", "");
}

/**
 * @brief 主机测试入口。
 * @return 0 表示全部通过。
 */
int main(void)
{
    test_no_track_shows_none_without_request();
    test_track_shows_blank_and_writes_a_request_for_its_identity();
    test_waits_for_the_reply_without_writing_again();
    test_accepts_the_reply_for_the_same_identity();
    test_a_generation_zero_reply_shows_none_without_rewriting();
    test_an_identity_change_clears_the_name_and_writes_a_newer_request();
    test_a_reply_to_an_overwritten_request_is_discarded_and_the_newer_one_is_accepted();
    test_losing_the_track_shows_none();
    test_a_request_that_could_not_be_written_is_retried_on_the_next_frame();
    test_cold_start_keeps_the_name_when_the_reply_generation_matches();
    test_cold_start_rewrites_when_the_name_generation_differs();
    test_cold_start_accepts_an_in_flight_reply_for_the_confirmed_generation();
    test_cold_start_discards_an_in_flight_reply_for_another_generation_and_rewrites();
    test_next_burst_accepts_only_the_last_request();
    test_losing_the_track_while_a_request_is_in_flight_discards_without_a_new_request();
    test_a_bad_file_skip_writes_a_request_for_the_candidate();
    test_a_frozen_identity_keeps_the_name_without_writing();
    test_the_foreground_answering_the_same_request_again_after_the_catalog_is_published_shows_the_name();
    test_accept_applies_title_and_artist_together();
    test_accept_without_artist_applies_unknown_artist();
    test_switch_clears_both_then_applies_the_new_pair();
    test_next_burst_never_applies_an_intermediate_artist();
    test_a_bad_file_skip_applies_the_candidate_pair();
    test_a_stale_reply_applies_nothing();
    test_cold_start_confirmation_keeps_the_pair();
    test_none_states_apply_dash_and_an_empty_artist();
    printf("gui_music_title tests passed\n");
    return 0;
}
