/**
  ******************************************************************************
  * @file    gui_music_title.c
  * @brief   音乐分区私有的曲名客户：身份采信、名称槽请求与应答采信。
  *
  * @details
  *          每圈两步：先按快照归约给出的身份决定是否换身份（换即清空旧名），
  *          再按槽状态推进一次握手。应答一律按“请求时的身份”判断，身份已变就丢弃，
  *          写回 IDLE 后下一圈按最新身份重请求，因此连续切歌只会采信最后一首。
  *          唯一例外是冷启动的代次确认（同一位置代次 0 → G）：名字或在途应答的代次
  *          等于 G 时视为同一首，不清空、不重请求。
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
 * @brief 采信新身份：清空旧名；有当前曲时留空并待请求，否则显示 `--`。
 * @param[in,out] client 曲名客户状态。
 * @param[in] identity 新身份。
 * @note  在途请求不撤销：槽忙时只能等它 READY 后丢弃，再按最新身份请求。
 */
static void gui_music_title_adopt(
    GUI_MusicTitleClientTypeDef *client,
    const GUI_MusicTitleIdentityTypeDef *identity)
{
    client->has_identity = true;
    client->identity = *identity;
    client->need_request = identity->has_track;
    client->shown = identity->has_track ? GUI_MUSIC_TITLE_SHOW_BLANK : GUI_MUSIC_TITLE_SHOW_NONE;
}

/**
 * @brief 复位为尚未采信任何身份、没有在途请求，显示 `--`。
 * @param[out] client 音乐分区持有的曲名客户状态。
 */
void gui_music_title_client_init(GUI_MusicTitleClientTypeDef *client)
{
    client->has_identity = false;
    client->identity.has_track = false;
    client->identity.generation = 0U;
    client->identity.index = 0U;
    client->need_request = false;
    client->in_flight = false;
    client->requested = client->identity;
    client->shown = GUI_MUSIC_TITLE_SHOW_NONE;
    client->name_generation = 0U;
    client->has_applied = false;
    client->applied = GUI_MUSIC_TITLE_SHOW_NONE;
}

/**
 * @brief 推进曲名客户一步：先采信身份，再按槽状态决定本圈动作。
 * @param[in,out] client 曲名客户状态。
 * @param[in] identity 快照归约本圈已追上的曲目身份；NULL 视为无当前曲。
 * @param[in] status 名称槽当前状态，取值须与 IDLE/PENDING/READY 宏一致。
 * @param[in] reply_generation 槽 READY 时的应答代次；0 表示取不到。其他状态下忽略。
 * @return 本圈对槽的动作。ACCEPT 与 DISCARD 之后调用方必须把槽写回 IDLE；
 *         REQUEST 失败时调用方须调 gui_music_title_request_failed()。
 * @note  READY 不属于在途请求（复位前遗留）时同样 DISCARD，避免槽停在 READY。
 */
GUI_MusicTitleActionTypeDef gui_music_title_poll(
    GUI_MusicTitleClientTypeDef *client,
    const GUI_MusicTitleIdentityTypeDef *identity,
    uint32_t status,
    uint32_t reply_generation)
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
        const bool reply_pending_for_previous =
            client->in_flight && gui_music_title_same_identity(&client->requested, &client->identity);

        if (confirmation && (name_is_confirmed || reply_pending_for_previous))
        {
            /* 冷启动代次确认：已显示的名字正是代次 G 的应答就保留，不闪空白；
               代次 0 的请求仍在途则等应答，按应答代次是否为 G 决定采信或重请求。 */
            client->identity = current;
        }
        else
        {
            gui_music_title_adopt(client, &current);
        }
    }

    if (status == GUI_MUSIC_TITLE_READY)
    {
        /* 应答按请求时的身份判断；代次 0 的请求在途时快照已确认为 G，应答代次为 G 也算当前。 */
        const bool reply_current =
            client->in_flight &&
            (gui_music_title_same_identity(&client->requested, &client->identity) ||
             (gui_music_title_is_confirmation(&client->requested, &client->identity) &&
              (reply_generation == client->identity.generation)));

        if (!reply_current)
        {
            /* 自己的应答已过期：下一圈按最新身份重请求。复位前遗留的 READY 只丢弃。 */
            if (client->in_flight)
            {
                client->need_request = client->identity.has_track;
            }
            client->in_flight = false;
            return GUI_MUSIC_TITLE_ACTION_DISCARD;
        }

        client->in_flight = false;

        if (reply_generation == 0U)
        {
            client->shown = GUI_MUSIC_TITLE_SHOW_NONE;
            return GUI_MUSIC_TITLE_ACTION_DISCARD;
        }

        client->shown = GUI_MUSIC_TITLE_SHOW_NAME;
        client->name_generation = reply_generation;
        return GUI_MUSIC_TITLE_ACTION_ACCEPT;
    }

    if ((status == GUI_MUSIC_TITLE_IDLE) && client->need_request && !client->in_flight)
    {
        client->need_request = false;
        client->in_flight = true;
        client->requested = client->identity;
        return GUI_MUSIC_TITLE_ACTION_REQUEST;
    }

    return GUI_MUSIC_TITLE_ACTION_NONE;
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
 * @brief 本圈 REQUEST 应提交的身份。
 * @param[in] client 曲名客户状态。
 * @return 在途请求所用的身份；其代次与位置即名称槽 request 的参数。
 */
GUI_MusicTitleIdentityTypeDef gui_music_title_request_identity(
    const GUI_MusicTitleClientTypeDef *client)
{
    return client->requested;
}

/**
 * @brief 名称槽拒绝了本圈的 request：撤销在途标记，下一圈重试。
 * @param[in,out] client 曲名客户状态。
 */
void gui_music_title_request_failed(GUI_MusicTitleClientTypeDef *client)
{
    client->in_flight = false;
    client->need_request = client->identity.has_track;
}

/**
 * @brief 在 poll 之后决定本圈要不要 TitleApply，以及 Apply 哪一对曲名与歌手。
 * @param[in,out] client 曲名客户状态；记下本圈交出的显示。
 * @param[in] action 本圈 gui_music_title_poll() 的返回值。
 * @param[in] slot_title 名称槽里的曲名；只在 action 为 ACCEPT 时读。
 * @param[in] slot_artist 名称槽里的歌手；只在 action 为 ACCEPT 时读。NULL 或空串交出
 *            GUI_MUSIC_TEXT_UNKNOWN_ARTIST。
 * @param[out] text 返回 true 时写入两列；指针指向槽内文本或常量文案。
 * @return true 表示本圈须 Apply：ACCEPT 时一律交出槽内名字；否则只在空白 / `--`
 *         与上次交出的显示不同时交出。
 * @note  曲名与歌手由同一次应答给出，同时出现、同时清空（GUI-26）。指向槽内的指针
 *        只在调用方把槽写回 IDLE 之前有效。
 */
bool gui_music_title_text(
    GUI_MusicTitleClientTypeDef *client,
    GUI_MusicTitleActionTypeDef action,
    const char *slot_title,
    const char *slot_artist,
    GUI_MusicTitleTextTypeDef *text)
{
    const GUI_MusicTitleShowTypeDef shown = client->shown;

    if (action == GUI_MUSIC_TITLE_ACTION_ACCEPT)
    {
        text->title = slot_title;
        /* 取不到歌手是常态：与 Queue 同一份界面文案，只在 GUI 侧替换（GUI-25）。 */
        text->artist = ((slot_artist == NULL) || (slot_artist[0] == '\0')) ? GUI_MUSIC_TEXT_UNKNOWN_ARTIST
                                                                          : slot_artist;
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
