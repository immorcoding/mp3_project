/**
  ******************************************************************************
  * @file    music_sim.c
  * @brief   假音乐前台流程：只替换音乐前台流程，窗口与曲名的应答经真实的格子（`ui_cell`）发布。
  *
  * @details
  *          曲库是 16 首固定曲目：原 10 首英文曲目（与旧演示分区一致）、5 个中文与文件名规则样本、一首需要滚动的超长英文名；
  *          `E` 键可把曲库换成空库。窗口行与曲名都按生产给出 {曲目显示名, 歌手}：music_sim_tags 里的曲目给出假 TIT2/TPE1
  *          （替代生产读标签），其余经真实的 catalog_display_name() 回退文件主名、歌手为空。
  *
  *          **保持“一次答一个”的节奏**（窗口和曲名各一个应答者，#200）：
  *          - 手上有需求时不看新需求，等够延迟后回答手上这个；
  *          - 答完隔一圈才取最新值（等同旧协议里“写回 IDLE 后下一圈才能请求”）；
  *          - 最新值和上次答过的相同就不答；启动和重新挂载时必答（上次答过的作废），同样隔一圈；
  *          - 每圈顺序仍是 假前台 → 假播放 → GUI step，所以应答落在和旧假 Storage 相同的圈上，42 个场景的帧哈希不变。
  *
  *          **代价：模拟器测不到“新需求覆盖旧需求”**。真实的前台流程每圈只读一行，新需求随时覆盖旧需求，已读的行靠行缓存不重读；
  *          假后端为了让应答落在和今天相同的圈上，手上有需求时不看新需求。“覆盖”由 `Tests/music_foreground`（主机）和
  *          第 5 次上板负责。“凑齐才发布”“代次变化发空窗”“发布后重新发窗”同理由主机测试覆盖。
  *
  *          `G`、`E` 键只把代次加 1，不重发窗口：产品里曲库发布后前台会马上重发，但发布到重发之间确实有一小段时间，旧窗口还在、
  *          点旧行得到 `Library changed`；假后端把这段时间拉长，`music_stop_reasons` 才拍得到。`C` 键：拔卡时当圈发代次 0 的
  *          空窗（GUI 当圈清空 Queue），有在途需求就当圈答空；重新挂载后隔一圈按最新需求应答。
  *
  *          播放游标归播放任务（假后端 playback_sim.c），本替身只给出当前代次与曲目数供假后端判定开启结果。SD 无消抖：
  *          已挂载即就绪。
  ******************************************************************************
  */

#include "music_sim.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "APP/apps/music/catalog/catalog_name.h"
#include "APP/apps/music/foreground/music_foreground.h"
#include "APP/apps/music/music_cells.h"
#include "APP/tasks/audio_reader/audio_reader_block.h"
#include "APP/tasks/foreground/foreground_task.h"

/*
 * 固定曲库的曲库相对路径；窗口行与曲名的曲目显示名在无假标签时都由它经真实的
 * catalog_display_name() 提取。原 10 首英文曲目没有目录与扩展名，提取结果与
 * 原文相同；第 10–14 首是中文与文件名规则样本，位于超长英文名之前，
 * 使开播后“上一首”仍绕到超长英文名，再按上一首依次得到中文短名、中英混排、中文长名、缺字与点号名。
 * 共 16 首，超过一窗 12 条，Queue 滚动到末尾时按窗口协议换窗。
 */
static const char *const music_sim_titles[] = {
    "Clair de Lune",
    "Gymnopedie No.1",
    "River Flows in You",
    "Nuvole Bianche",
    "Comptine d'un autre ete",
    "Experience",
    "Merry Christmas Mr. Lawrence",
    "Kiss the Rain",
    "Una Mattina",
    "Spring Waltz",
    "Music/a.b.MP3",                                 /* 点号与大写扩展名：显示 `a.b`。 */
    "Music/繁體 龍 喆 镕.mp3",                       /* 體、龍、喆、镕不在 GB2312：缺字画占位框。 */
    "Music/周杰伦 - 晴天（2003 年演唱会现场版）故事的小黄花从出生那年就飘着.mp3", /* 中文长名，需要滚动。 */
    "Music/Jay Chou 周杰伦 - 晴天.mp3",              /* 中英混排。 */
    "Music/晴天.mp3",                                /* 中文短名。 */
    "An Unusually Long English Track Name That Has To Scroll Across Now Playing",
};

#define MUSIC_SIM_TITLE_COUNT \
    ((uint16_t)(sizeof(music_sim_titles) / sizeof(music_sim_titles[0])))

/** @brief 一首曲目的假标签：替代生产读标签的“取到”结果。 */
typedef struct
{
    uint16_t Index;      /* 播放列表位置。 */
    const char *Title;   /* 可用 TIT2（UTF-8）。 */
    const char *Artist;  /* 可用 TPE1（UTF-8）；NULL 表示歌手取不到。 */
} MusicSim_TagTypeDef;

/*
 * 有可用 TIT2 的曲目；其余曲目视为无标签，回退文件主名、歌手为空（同生产的标签侧回退）。
 * 第 6 首（位置 5）有标题与歌手，标题与文件主名不同，唱盘页可据此区分来源；第 8 首（位置 7）
 * 有标题、无歌手。Queue 第一窗由此混排三类行态（GUI-25）：标题 + 歌手、标题无歌手、
 * 回退文件主名（歌手空）。第 10 首（位置 9）的标题带 Latin-1 补充区字符与 U+2013、U+2122
 * （`®`、`é`、`–`、`™`，#148），唱盘页与 Queue 用资源包字体显示，歌手宽于 Queue 一行。第 9 首（位置 8）是中文标题 +
 * 中文歌手（#149），唱盘页歌手场景 `music_artist` 也经过它。第 7 首（位置 6）的标题与文件主名相同、
 * 歌手为多值 TPE1 按 ` / ` 连写的结果（#154），唱盘页与 Queue 都显示；其余标签为 ASCII。
 */
static const MusicSim_TagTypeDef music_sim_tags[] = {
    {6U, "Merry Christmas Mr. Lawrence", "Ryuichi Sakamoto / David Sylvian"},
    {7U, "Kiss the Rain (Live)", NULL},
    {5U, "Experience (Einaudi Live)", "Ludovico Einaudi"},
    {8U, "晴天（2003 年演唱会现场版）", "周杰伦"},
    {9U, "Beyoncé® – Halo™", "Beyoncé & F1® The Movie Orchestra – Live™ Édition"}, /* 歌手宽于一行：截断加省略号。 */
};

#define MUSIC_SIM_TAG_COUNT \
    ((uint16_t)(sizeof(music_sim_tags) / sizeof(music_sim_tags[0])))

/** @brief 一个应答者的节奏状态：窗口和曲名各一个，“一次答一个”。 */
typedef struct
{
    bool hand;            /* 手上有需求：等够延迟再答，期间不看新需求。 */
    uint32_t waited;      /* 手上这个需求已经等了几圈。 */
    uint32_t cooldown;    /* 答完或重新挂载之后还要隔几圈才取最新值。 */
    uint32_t delay;       /* 额外等待的圈数（脚本 --window-delay / --name-delay）。 */
} MusicSim_ResponderTypeDef;

static bool music_sim_is_mounted;
static bool music_sim_empty;
static uint32_t music_sim_generation;
static bool music_sim_name_fail[MUSIC_SIM_TITLE_COUNT]; /* 为真的位置名字取不到。 */

static MusicSim_ResponderTypeDef music_sim_window_responder;
static MusicSim_ResponderTypeDef music_sim_title_responder;

static MusicCells_WindowRequestTypeDef music_sim_window_request; /* 最新窗口需求副本。 */
static MusicCells_TitleRequestTypeDef music_sim_title_request;   /* 最新曲名需求副本。 */
static uint32_t music_sim_window_request_seen;
static uint32_t music_sim_title_request_seen;
static bool music_sim_window_answered_valid;   /* 上次答过的窗口起点有效。 */
static uint16_t music_sim_window_answered_index;
static bool music_sim_title_answered_valid;    /* 上次答过的曲名需求序号有效。 */
static uint32_t music_sim_title_answered_sequence;
static MusicCells_WindowRequestTypeDef music_sim_window_hand;
static MusicCells_TitleRequestTypeDef music_sim_title_hand;

static MusicCells_WindowTypeDef music_sim_window_result;
static MusicCells_TitleResultTypeDef music_sim_title_result;

/**
 * @brief 同生产曲目文本：有假 TIT2 取标题与歌手，否则回退文件主名、歌手为空。
 * @param[in] index 播放列表位置，须在固定曲库范围内。
 * @param[out] title 曲目显示名，容量 CATALOG_NAME_BYTES。
 * @param[out] artist 歌手，容量 CATALOG_NAME_BYTES；取不到为空串。
 * @return false 表示文件主名提取失败（生产的防御分支），此时两列为空串。
 */
static bool music_sim_track_text(uint16_t index, char *title, char *artist)
{
    uint16_t i;

    artist[0] = '\0';
    if (!catalog_display_name(music_sim_titles[index],
                              (uint32_t)strlen(music_sim_titles[index]),
                              title,
                              CATALOG_NAME_BYTES))
    {
        title[0] = '\0';
        return false;
    }

    for (i = 0U; i < MUSIC_SIM_TAG_COUNT; i++)
    {
        if (music_sim_tags[i].Index == index)
        {
            (void)snprintf(title, CATALOG_NAME_BYTES, "%s", music_sim_tags[i].Title);
            if (music_sim_tags[i].Artist != NULL)
            {
                (void)snprintf(artist, CATALOG_NAME_BYTES, "%s", music_sim_tags[i].Artist);
            }
            break;
        }
    }

    return true;
}

/**
 * @brief 复位为已挂载、第 1 代、固定曲库非空，两个应答者回到初始节奏。
 * @note  须在 gui_music_init() 之前调用。
 */
void music_sim_init(void)
{
    music_sim_is_mounted = true;
    music_sim_empty = false;
    music_sim_generation = 1U;
    (void)memset(music_sim_name_fail, 0, sizeof(music_sim_name_fail));
    (void)memset(&music_sim_window_responder, 0, sizeof(music_sim_window_responder));
    (void)memset(&music_sim_title_responder, 0, sizeof(music_sim_title_responder));
    (void)memset(&music_sim_window_request, 0, sizeof(music_sim_window_request));
    (void)memset(&music_sim_title_request, 0, sizeof(music_sim_title_request));
    music_sim_window_request_seen = 0U;
    music_sim_title_request_seen = 0U;
    music_sim_window_answered_valid = false;
    music_sim_title_answered_valid = false;
}

/**
 * @brief 发布一份窗口结果：整份写进结果格（写后不叫醒 GUI）。
 * @param[in] generation 实际代次；0 表示空窗。
 * @param[in] index 窗口起点。
 * @param[in] length 窗口条数。
 */
static void music_sim_publish_window(uint32_t generation, uint16_t index, uint16_t length)
{
    music_sim_window_result.Published = true;
    music_sim_window_result.Generation = generation;
    music_sim_window_result.Index = index;
    music_sim_window_result.Length = length;
    ui_cell_write(&music_cell_window_result, &music_sim_window_result);
}

/**
 * @brief 发布一份曲名结果：整份写进结果格。
 * @param[in] sequence 所答的需求序号。
 * @param[in] generation 实际代次；0 表示取不到，两列为空串。
 * @param[in] index 播放列表位置（generation 非 0 时取名字）。
 */
static void music_sim_publish_title(uint32_t sequence, uint32_t generation, uint16_t index)
{
    music_sim_title_result.RequestSequence = sequence;
    music_sim_title_result.Generation = generation;
    music_sim_title_result.Title[0] = '\0';
    music_sim_title_result.Artist[0] = '\0';
    if (generation != 0U)
    {
        (void)music_sim_track_text(index, music_sim_title_result.Title, music_sim_title_result.Artist);
    }
    ui_cell_write(&music_cell_title_result, &music_sim_title_result);
}

/**
 * @brief 同生产：按当前代次从固定曲目填整窗 {曲目显示名, 歌手} 并发布。
 * @param[in] index 窗口起点。
 * @note  不做逐行延迟：生产窗口填满前不发布，GUI 只见整窗，一次填完与逐圈一行在 GUI 侧不可区分；逐圈排程进板级验收。
 */
static void music_sim_answer_window(uint16_t index)
{
    uint16_t filled = 0U;
    uint16_t i;

    for (i = 0U;
         (i < MUSIC_WINDOW_MAX_ENTRIES) && ((uint32_t)index + i < music_sim_track_count());
         i++)
    {
        /* 同生产：文件主名也提取不出时该行两列为空串，不中断整窗。 */
        (void)music_sim_track_text((uint16_t)(index + i),
                                   music_sim_window_result.Rows[i].Title,
                                   music_sim_window_result.Rows[i].Artist);
        filled++;
    }

    music_sim_publish_window(music_sim_generation, index, filled);
}

/**
 * @brief 打印与生产 WARN 同形的一行 `Name fail idx=N gen=G`（G 为请求代次）。
 * @param[in] request 曲名需求。
 */
static void music_sim_name_fail_report(const MusicCells_TitleRequestTypeDef *request)
{
    printf("[music] Name fail idx=%u gen=%u\n", (unsigned)request->Index, (unsigned)request->Generation);
}

/**
 * @brief 同生产：按当前代次给出固定曲库的曲目显示名与歌手并发布。
 * @param[in] request 手上的曲名需求。
 * @note  未挂载或代次不符答代次 0 空名，不记日志。代次相符（或请求为“当前”）仍越界/提取失败、
 *        以及 --name-fail 标记的位置走生产的防御分支：答代次 0 空名，并打印与生产 WARN 同形的一行
 *        （请求为“当前”的越界是空库开播的正常过程，不记）。
 */
static void music_sim_answer_title(const MusicCells_TitleRequestTypeDef *request)
{
    if (!music_sim_is_mounted ||
        ((request->Generation != 0U) && (request->Generation != music_sim_generation)))
    {
        music_sim_publish_title(request->Sequence, 0U, 0U);
        return;
    }

    /* 同生产：请求为“当前”时越界是空库开播的正常过程，不记；代次相符的越界与提取失败才是防御分支。 */
    if (request->Index >= music_sim_track_count())
    {
        if (request->Generation != 0U)
        {
            music_sim_name_fail_report(request);
        }
        music_sim_publish_title(request->Sequence, 0U, 0U);
        return;
    }

    /* --name-fail 与文件主名提取失败都是生产的防御分支：答代次 0 空名并打印同形 WARN。 */
    if (music_sim_name_fail[request->Index])
    {
        music_sim_name_fail_report(request);
        music_sim_publish_title(request->Sequence, 0U, 0U);
        return;
    }

    music_sim_publish_title(request->Sequence, music_sim_generation, request->Index);
}

/**
 * @brief 替身的前台内存准备：整份复位（挂载、第 1 代、固定曲库、应答节奏），与生产的 `music_foreground_boot_init()` 同名。
 * @note  Boot 之前调用的角色由 sim_main.c 在 `app_table_boot_init()` 里扮演；格子由真实的 `music_cells_boot_init()` 初始化。
 */
void music_foreground_boot_init(void)
{
    music_sim_init();
}

/**
 * @brief 代替前台任务走一圈（注册表的 foreground_step）：窗口和曲名各一个应答者，节奏见文件头。
 * @return 总是等通知：模拟器每圈都调，不看返回值。
 * @note  每圈在 gui_music_step() 之前调用，因此需求写在上一圈、应答恰好落在本圈可读。卸载期间不取新需求（拔卡的空窗已在
 *        `music_sim_toggle_mount()` 里当圈发出）。
 */
uint32_t music_foreground_step(void)
{
    MusicCells_WindowRequestTypeDef window_request;
    MusicCells_TitleRequestTypeDef title_request;

    /* 窗口应答者。 */
    if (!music_sim_window_responder.hand)
    {
        if (music_sim_window_responder.cooldown > 0U)
        {
            music_sim_window_responder.cooldown--;
        }
        else if (music_sim_is_mounted)
        {
            if (ui_cell_read_if_changed(&music_cell_window_request, &window_request, &music_sim_window_request_seen))
            {
                music_sim_window_request = window_request;
            }
            if (music_sim_window_request.Valid &&
                (!music_sim_window_answered_valid || (music_sim_window_request.Index != music_sim_window_answered_index)))
            {
                music_sim_window_responder.hand = true;
                music_sim_window_responder.waited = 0U;
                music_sim_window_hand = music_sim_window_request;
            }
        }
    }
    if (music_sim_window_responder.hand)
    {
        if (music_sim_window_responder.waited < music_sim_window_responder.delay)
        {
            music_sim_window_responder.waited++;
        }
        else
        {
            music_sim_window_responder.hand = false;
            music_sim_window_responder.cooldown = 1U;
            music_sim_window_answered_valid = true;
            music_sim_window_answered_index = music_sim_window_hand.Index;
            music_sim_answer_window(music_sim_window_hand.Index);
        }
    }

    /* 曲名应答者。 */
    if (!music_sim_title_responder.hand)
    {
        if (music_sim_title_responder.cooldown > 0U)
        {
            music_sim_title_responder.cooldown--;
        }
        else
        {
            if (ui_cell_read_if_changed(&music_cell_title_request, &title_request, &music_sim_title_request_seen))
            {
                music_sim_title_request = title_request;
            }
            if (music_sim_title_request.Wanted &&
                (!music_sim_title_answered_valid || (music_sim_title_request.Sequence != music_sim_title_answered_sequence)))
            {
                music_sim_title_responder.hand = true;
                music_sim_title_responder.waited = 0U;
                music_sim_title_hand = music_sim_title_request;
            }
        }
    }
    if (music_sim_title_responder.hand)
    {
        if (music_sim_title_responder.waited < music_sim_title_responder.delay)
        {
            music_sim_title_responder.waited++;
        }
        else
        {
            music_sim_title_responder.hand = false;
            music_sim_title_responder.cooldown = 1U;
            music_sim_title_answered_valid = true;
            music_sim_title_answered_sequence = music_sim_title_hand.Sequence;
            music_sim_answer_title(&music_sim_title_hand);
        }
    }

    return FOREGROUND_WAIT_FOREVER;
}

/**
 * @brief C 键：在拔卡与重新挂载之间切换，并把代次 +1。
 * @return 切换后是否已挂载；为假时调用方应让假播放后端按拔卡停止。
 * @note  拔卡：当圈发代次 0 的空窗（GUI 当圈清空 Queue，今天 GUI 也是当圈清空），有在途的曲名需求就当圈答空；上次答过的作废。
 *        重新挂载：隔一圈按最新需求应答（上次答过的作废，所以同一个需求也答）。
 */
bool music_sim_toggle_mount(void)
{
    music_sim_generation++;
    if (music_sim_is_mounted)
    {
        music_sim_is_mounted = false;
        music_sim_publish_window(0U, 0U, 0U);
        music_sim_window_responder.hand = false;
        music_sim_window_responder.cooldown = 0U;
        music_sim_window_answered_valid = false;
        if (music_sim_title_responder.hand)
        {
            music_sim_title_responder.hand = false;
            music_sim_title_responder.cooldown = 0U;
            music_sim_publish_title(music_sim_title_hand.Sequence, 0U, 0U);
        }
        printf("[music] card removed (generation %u)\n", (unsigned)music_sim_generation);
    }
    else
    {
        music_sim_is_mounted = true;
        music_sim_window_responder.cooldown = 1U;
        music_sim_window_answered_valid = false;
        printf("[music] mounted (generation %u)\n", (unsigned)music_sim_generation);
    }

    return music_sim_is_mounted;
}

/**
 * @brief G 键：模拟重扫，代次 +1，不重发窗口。
 * @note  已显示的 Queue 窗口不主动作废，保留到音乐分区下一次要窗，用于制造过期窗口（`Library changed`）。未挂载时忽略。
 */
void music_sim_rescan(void)
{
    if (!music_sim_is_mounted)
    {
        return;
    }

    music_sim_generation++;
    printf("[music] rescan (generation %u)\n", (unsigned)music_sim_generation);
}

/**
 * @brief E 键：模拟重扫得到空库（再按一次恢复固定曲库），代次 +1，不重发窗口。
 * @note  未挂载时忽略。已显示的 Queue 窗口与 G 键一样保留到下一次要窗。
 */
void music_sim_toggle_empty(void)
{
    if (!music_sim_is_mounted)
    {
        return;
    }

    music_sim_empty = !music_sim_empty;
    music_sim_generation++;
    printf("[music] rescan %s (generation %u)\n",
           music_sim_empty ? "empty" : "restored",
           (unsigned)music_sim_generation);
}

/**
 * @brief 脚本 --name-fail：切换第 index 首的名字是否取不到。
 * @param[in] index 播放列表位置；超出固定曲库时忽略。
 * @note  取不到时按生产防御分支答代次 0 空名，并打印与生产 WARN 同形的一行。
 */
void music_sim_toggle_name_fail(uint16_t index)
{
    if (index >= MUSIC_SIM_TITLE_COUNT)
    {
        return;
    }

    music_sim_name_fail[index] = !music_sim_name_fail[index];
    printf("[music] name of idx=%u %s\n",
           (unsigned)index,
           music_sim_name_fail[index] ? "unavailable" : "available");
}

/**
 * @brief 脚本 --name-delay：曲名需求在手上后多等 loops 圈再应答。
 * @param[in] loops 额外等待的圈数；0 恢复为下一圈应答。
 */
void music_sim_set_name_delay(uint32_t loops)
{
    music_sim_title_responder.delay = loops;
    printf("[music] name reply delay %u loop(s)\n", (unsigned)loops);
}

/**
 * @brief 脚本 --window-delay：窗口需求在手上后多等 loops 圈再应答。
 * @param[in] loops 额外等待的圈数；0 恢复为下一圈应答。
 * @note  生产前台每圈只读一行标签，整窗就绪要等十余圈；默认一圈填完拍不到这段空窗期。
 */
void music_sim_set_window_delay(uint32_t loops)
{
    music_sim_window_responder.delay = loops;
    printf("[music] window reply delay %u loop(s)\n", (unsigned)loops);
}

/**
 * @brief 卷是否已挂载，供假播放后端判定开启结果。
 * @return 已挂载为真。
 */
bool music_sim_mounted(void)
{
    return music_sim_is_mounted;
}

/**
 * @brief 当前曲库代次，供假播放后端判定开启是否过期（STALE）。
 * @return 当前代次；从 1 开始，挂载切换与重扫各 +1。
 */
uint32_t music_sim_generation_get(void)
{
    return music_sim_generation;
}

/**
 * @brief 当前曲库的曲目数，供假播放后端判定越界与环形步进。
 * @return 曲目数；空库为 0。
 */
uint16_t music_sim_track_count(void)
{
    return music_sim_empty ? 0U : MUSIC_SIM_TITLE_COUNT;
}

/** @brief 替身：叫醒前台任务。模拟器每圈都调假前台，不需要叫醒。 */
void foreground_task_wake(void)
{
}

/** @brief 替身：块池就绪数。翻页注入钩子只在固件 Debug 的钩子构建里用，模拟器里钩子关闭，这里给 0 让符号链接得上。 @return 0。 */
uint32_t audio_reader_block_ready_count(void)
{
    return 0U;
}

/** @brief 替身：块池空闲数，同上。 @return 0。 */
uint32_t audio_reader_block_free_count(void)
{
    return 0U;
}
