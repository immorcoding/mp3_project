/**
  ******************************************************************************
  * @file    gui_music_title.c
  * @brief   音乐分区私有的曲名客户：身份采信、曲名需求的写出与结果的采信。
  *
  * @details
  *          每帧两步：先按快照归约给出的身份决定是否换身份（换即清空旧名、待写新需求），再处理读到的新结果。结果一律按“所答的需求序号”
  *          判断：序号不等于最新写出的需求就是过期（身份已变、需求已被覆盖），丢弃；因此连续切歌只会采信最后一首。
  *          唯一例外是冷启动的代次确认（同一位置代次 0 → G）：名字或在途结果的代次等于 G 时视为同一首，不清空、不重写需求
  *          （需求序号不变）。需求写不进去（拿不到锁）时保持“待写”，下一帧再试。
  ******************************************************************************
  */

#include "gui_music_title.h"
#include "gui_music_text.h"

#include <stddef.h>

/**
 * @brief 判断两个曲目身份是否相同。
 * @param[in] a 身份一。
 * @param[in] b 身份二。
 * @return 无当前曲时只比较“无”；否则代次与位置都相等才为真。
 */
static bool gui_music_title_same_identity(
    const GUI_MusicTitleIdentityTypeDef *a,
    const GUI_MusicTitleIdentityTypeDef *b)
{
    if (a->has_track != b->has_track)
    {
        return false;
    }

    return !a->has_track ||
           ((a->generation == b->generation) && (a->index == b->index));
}

/**
 * @brief 判断 to 是否只是 from 的冷启动代次确认：同一位置，代次从 0（开启未确认）变为非 0。
 * @param[in] from 原身份。
 * @param[in] to 新身份。
 * @return 两者都有当前曲、位置相同、from 代次为 0 且 to 代次非 0 时为真。
 */
static bool gui_music_title_is_confirmation(
    const GUI_MusicTitleIdentityTypeDef *from,
    const GUI_MusicTitleIdentityTypeDef *to)
{
    return from->has_track && to->has_track &&
           (from->generation == 0U) && (to->generation != 0U) &&
           (from->index == to->index);
}

/**
 * @brief 采信新身份：清空旧名；有当前曲时留空并待写需求，否则显示 `--`。
 * @param[in,out] client 曲名客户状态。
 * @param[in] identity 新身份。
 * @note  已写出的需求不撤销：前台任务可能已经在读它，结果到达时序号落后，被丢弃。
 */
static void gui_music_title_adopt(
    GUI_MusicTitleClientTypeDef *client,
    const GUI_MusicTitleIdentityTypeDef *identity)
{
    client->has_identity = true;
    client->identity = *identity;
    client->write_pending = identity->has_track;
    client->reply_open = false;
    client->answered = false;
    client->shown = identity->has_track ? GUI_MUSIC_TITLE_SHOW_BLANK : GUI_MUSIC_TITLE_SHOW_NONE;
}

/**
 * @brief 复位为尚未采信任何身份、没有写过需求，显示 `--`。
 * @param[out] client 音乐分区持有的曲名客户状态。
 */
void gui_music_title_client_init(GUI_MusicTitleClientTypeDef *client)
{
    client->has_identity = false;
    client->identity.has_track = false;
    client->identity.generation = 0U;
    client->identity.index = 0U;
    client->sequence = 0U;
    client->request_generation = 0U;
    client->write_pending = false;
    client->reply_open = false;
    client->answered = false;
    client->shown = GUI_MUSIC_TITLE_SHOW_NONE;
    client->name_generation = 0U;
    client->has_applied = false;
    client->applied = GUI_MUSIC_TITLE_SHOW_NONE;
}

/**
 * @brief 推进曲名客户一帧：先采信身份，再按读到的新结果决定动作。
 * @param[in,out] client 曲名客户状态。
 * @param[in] identity 快照归约本帧已追上的曲目身份；NULL 视为无当前曲。
 * @param[in] reply 本帧从结果格读到的新结果；没有新结果为 NULL。
 * @return 对新结果的动作：ACCEPT 显示结果里的名字；DISCARD 不读名字；没有新结果为 NONE。
 * @note  序号不等于最新写出的需求（或该需求已因身份变化而不再适用）的结果一律 DISCARD。同一序号的结果可以多次到达
 *        （曲库发布后前台主动重答一次）：每次都按同一规则判断，所以先到的代次 0（`--`）可以被后到的名字替换。
 */
GUI_MusicTitleActionTypeDef gui_music_title_poll(
    GUI_MusicTitleClientTypeDef *client,
    const GUI_MusicTitleIdentityTypeDef *identity,
    const MusicCells_TitleResultTypeDef *reply)
{
    GUI_MusicTitleIdentityTypeDef current = {false, 0U, 0U};

    if (identity != NULL)
    {
        current = *identity;
    }

    if (!client->has_identity)
    {
        gui_music_title_adopt(client, &current);
    }
    else if (!gui_music_title_same_identity(&current, &client->identity))
    {
        const bool confirmation = gui_music_title_is_confirmation(&client->identity, &current);
        const bool name_is_confirmed =
            (client->shown == GUI_MUSIC_TITLE_SHOW_NAME) &&
            (client->name_generation == current.generation);
        const bool reply_pending_for_previous = client->reply_open && !client->answered;

        if (confirmation && (name_is_confirmed || reply_pending_for_previous))
        {
            /* 冷启动代次确认：已显示的名字正是代次 G 的结果就保留，不闪空白；
               代次 0 的需求仍在途则等结果，按结果代次是否为 G 决定采信或重写。 */
            client->identity = current;
        }
        else
        {
            gui_music_title_adopt(client, &current);
        }
    }

    if (reply == NULL)
    {
        return GUI_MUSIC_TITLE_ACTION_NONE;
    }

    if (!client->reply_open || (reply->RequestSequence != client->sequence))
    {
        return GUI_MUSIC_TITLE_ACTION_DISCARD; /* 所答的需求已被覆盖：不显示旧名，新需求的结果随后到。 */
    }

    client->answered = true;

    if (reply->Generation == 0U)
    {
        if (client->request_generation != client->identity.generation)
        {
            /* 冷启动代次确认后，代次 0 的需求答了“取不到”：它答的不是 G，按 G 重写。 */
            client->write_pending = client->identity.has_track;
            client->reply_open = false;
            return GUI_MUSIC_TITLE_ACTION_DISCARD;
        }

        client->shown = GUI_MUSIC_TITLE_SHOW_NONE;
        return GUI_MUSIC_TITLE_ACTION_DISCARD;
    }

    if ((client->identity.generation != 0U) && (reply->Generation != client->identity.generation))
    {
        /* 结果的代次不是当前身份的代次（含冷启动确认后答的是别的代次）：丢弃并按当前身份重写。 */
        client->write_pending = client->identity.has_track;
        client->reply_open = false;
        return GUI_MUSIC_TITLE_ACTION_DISCARD;
    }

    client->shown = GUI_MUSIC_TITLE_SHOW_NAME;
    client->name_generation = reply->Generation;
    return GUI_MUSIC_TITLE_ACTION_ACCEPT;
}

/**
 * @brief 曲名区应显示的内容。
 * @param[in] client 曲名客户状态。
 * @return 名字 / 空白 / `--`。
 */
GUI_MusicTitleShowTypeDef gui_music_title_shown(const GUI_MusicTitleClientTypeDef *client)
{
    return client->shown;
}

/**
 * @brief 本帧是否该写（或重写）曲名需求。
 * @param[in] client 曲名客户状态。
 * @return 有当前曲且需求待写时为真；写失败后保持为真，直到 `gui_music_title_request_sent()`。
 */
bool gui_music_title_request_due(const GUI_MusicTitleClientTypeDef *client)
{
    return client->write_pending && client->identity.has_track;
}

/**
 * @brief 本帧要写进需求格的内容：下一个序号、当前身份的代次与位置。
 * @param[in] client 曲名客户状态。
 * @return 需求；序号是候选值，写成功后调用 `gui_music_title_request_sent()` 才算占用，写失败不占序号。
 */
MusicCells_TitleRequestTypeDef gui_music_title_request(const GUI_MusicTitleClientTypeDef *client)
{
    MusicCells_TitleRequestTypeDef request;

    request.Sequence = client->sequence + 1U;
    request.Generation = client->identity.generation;
    request.Index = client->identity.index;
    request.Wanted = client->identity.has_track;
    return request;
}

/**
 * @brief 需求已写进需求格：占用序号，之后等这个序号的结果。
 * @param[in,out] client 曲名客户状态。
 */
void gui_music_title_request_sent(GUI_MusicTitleClientTypeDef *client)
{
    client->sequence++;
    client->request_generation = client->identity.generation;
    client->write_pending = false;
    client->reply_open = true;
    client->answered = false;
}

/**
 * @brief 在 poll 之后决定本帧要不要 TitleApply，以及 Apply 哪一对曲名与歌手。
 * @param[in,out] client 曲名客户状态；记下本帧交出的显示。
 * @param[in] action 本帧 gui_music_title_poll() 的返回值。
 * @param[in] reply_title 结果副本里的曲名；只在 action 为 ACCEPT 时读。
 * @param[in] reply_artist 结果副本里的歌手；只在 action 为 ACCEPT 时读。NULL 或空串交出
 *            GUI_MUSIC_TEXT_UNKNOWN_ARTIST。
 * @param[out] text 返回 true 时写入两列；指针指向结果副本内的文本或常量文案。
 * @return true 表示本帧须 Apply：ACCEPT 时一律交出结果里的名字；否则只在空白 / `--`
 *         与上次交出的显示不同时交出。
 * @note  曲名与歌手由同一次结果给出，同时出现、同时清空（GUI-26）。指向结果副本的指针只在下一次读结果之前有效。
 */
bool gui_music_title_text(
    GUI_MusicTitleClientTypeDef *client,
    GUI_MusicTitleActionTypeDef action,
    const char *reply_title,
    const char *reply_artist,
    GUI_MusicTitleTextTypeDef *text)
{
    const GUI_MusicTitleShowTypeDef shown = client->shown;

    if (action == GUI_MUSIC_TITLE_ACTION_ACCEPT)
    {
        text->title = reply_title;
        /* 取不到歌手是常态：与 Queue 同一份界面文案，只在 GUI 侧替换（GUI-25）。 */
        text->artist = ((reply_artist == NULL) || (reply_artist[0] == '\0')) ? GUI_MUSIC_TEXT_UNKNOWN_ARTIST
                                                                           : reply_artist;
    }
    else if (!client->has_applied || (shown != client->applied))
    {
        text->title = (shown == GUI_MUSIC_TITLE_SHOW_BLANK) ? "" : "--";
        text->artist = "";
    }
    else
    {
        return false;
    }

    client->has_applied = true;
    client->applied = shown;
    return true;
}
