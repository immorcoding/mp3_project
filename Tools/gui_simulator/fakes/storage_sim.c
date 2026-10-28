/**
  ******************************************************************************
  * @file    storage_sim.c
  * @brief   Storage 替身：只替换音乐分区用到的窗口槽、名称槽与 SD 状态。
  *
  * @details
  *          按生产头实现 `storage_listbuffer` / `storage_namebuffer` 全局对象、
  *          `storage_listbuffer_*`、`storage_namebuffer_*` 与
  *          `storage_task_sd_is_ready/mounted()`。曲库是 16 首固定曲目：原 10 首英文
  *          曲目（与旧演示分区一致）、5 个中文与文件名规则样本、一首需要滚动的超长英文名；
  *          `E` 键可把曲库换成空库。窗口行与名称槽都按生产给出 {曲目显示名, 歌手}：
  *          storage_sim_tags 里的曲目给出假 TIT2/TPE1（替代生产读标签），其余经真实的
  *          storage_catalog_display_name() 回退文件主名、歌手为空。没有 Storage Task：
  *          sim_main.c 每圈先调 storage_sim_step()，上一圈 request 的窗口与名字在本圈填成 READY。
  *          名称槽另有两个脚本开关：让某首的名字取不到（按生产防御分支应答代次 0），
  *          让应答多等若干圈（制造空白帧与过期应答）。
  *
  *          播放游标归播放任务（假后端 playback_sim.c），本替身只给出当前代次与
  *          曲目数供假后端判定开启结果。SD 无消抖：已挂载即就绪。
  ******************************************************************************
  */

#include "storage_sim.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/storage/catalog/storage_catalog_name.h"
#include "APP/tasks/storage/catalog/storage_listbuffer.h"
#include "APP/tasks/storage/catalog/storage_namebuffer.h"

/*
 * 固定曲库的曲库相对路径；窗口行与名称槽的曲目显示名在无假标签时都由它经真实的
 * storage_catalog_display_name() 提取。原 10 首英文曲目没有目录与扩展名，提取结果与
 * 原文相同；第 10–14 首是中文与文件名规则样本，位于超长英文名之前，
 * 使开播后“上一首”仍绕到超长英文名，再按上一首依次得到中文短名、中英混排、中文长名、缺字与点号名。
 * 共 16 首，超过一窗 12 条，Queue 滚动到末尾时按窗口协议换窗。
 */
static const char *const storage_sim_titles[] = {
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

#define STORAGE_SIM_TITLE_COUNT \
    ((uint16_t)(sizeof(storage_sim_titles) / sizeof(storage_sim_titles[0])))

/** @brief 一首曲目的假标签：替代生产 storage_catalog_track_text() 的“取到”结果。 */
typedef struct
{
    uint16_t Index;      /* 播放列表位置。 */
    const char *Title;   /* 可用 TIT2，ASCII。 */
    const char *Artist;  /* 可用 TPE1，ASCII；NULL 表示歌手取不到。 */
} StorageSim_TagTypeDef;

/*
 * 有可用 TIT2 的曲目；其余曲目视为无标签，回退文件主名、歌手为空（同生产的标签侧回退）。
 * 第 6 首（位置 5）有标题与歌手，标题与文件主名不同，唱盘页可据此区分来源；第 8 首（位置 7）
 * 有标题、无歌手。Queue 第一窗由此混排三类行态（GUI-23）：标题 + 歌手、标题无歌手、
 * 回退文件主名（歌手空）。全部 ASCII。
 */
static const StorageSim_TagTypeDef storage_sim_tags[] = {
    /* PROTOTYPE #152：唱盘页歌手行的假标签，只在 --artist-layout 非 0 时生效（位置 1 不加，作“无歌手”）。 */
    {0U, "Clair de Lune", "Claude Debussy"},
    {2U, "晴天", "周杰伦"},
    {3U, "Nuvole Bianche", "Ludovico Einaudi / Yiruma"},
    {4U, "Comptine d'un autre ete", "Yann Tiersen / The Royal Philharmonic Orchestra / London Symphony Chorus"},
    {7U, "Kiss the Rain (Live)", NULL},
    {5U, "Experience (Einaudi Live)", "Ludovico Einaudi"},
};

#define STORAGE_SIM_TAG_COUNT \
    ((uint16_t)(sizeof(storage_sim_tags) / sizeof(storage_sim_tags[0])))

/* PROTOTYPE #152：表头 4 条只在 storage_sim_proto_tags 为真时生效，默认场景与基线不变。 */
#define STORAGE_SIM_PROTO_TAG_COUNT (4U)
bool storage_sim_proto_tags;

Storage_ListBufferTypeDef storage_listbuffer;
Storage_NameBufferTypeDef storage_namebuffer;

static bool storage_sim_mounted;
static bool storage_sim_empty;
static uint32_t storage_sim_generation;
static bool storage_sim_name_fail[STORAGE_SIM_TITLE_COUNT]; /* 为真的位置名字取不到。 */
static uint32_t storage_sim_name_delay;                     /* 名称槽应答额外等待的圈数。 */
static uint32_t storage_sim_name_waited;                    /* 当前 PENDING 已等待的圈数。 */

/**
 * @brief 同生产曲目文本：有假 TIT2 取标题与歌手，否则回退文件主名、歌手为空。
 * @param[in] index 播放列表位置，须在固定曲库范围内。
 * @param[out] title 曲目显示名，容量 STORAGE_CATALOG_NAME_BYTES。
 * @param[out] artist 歌手，容量 STORAGE_CATALOG_NAME_BYTES；取不到为空串。
 * @return false 表示文件主名提取失败（生产的防御分支），此时两列为空串。
 */
static bool storage_sim_track_text(uint16_t index, char *title, char *artist)
{
    uint16_t i;

    artist[0] = '\0';
    if (!storage_catalog_display_name(storage_sim_titles[index],
                                      (uint32_t)strlen(storage_sim_titles[index]),
                                      title,
                                      STORAGE_CATALOG_NAME_BYTES))
    {
        title[0] = '\0';
        return false;
    }

    for (i = storage_sim_proto_tags ? 0U : STORAGE_SIM_PROTO_TAG_COUNT; i < STORAGE_SIM_TAG_COUNT; i++)
    {
        if (storage_sim_tags[i].Index == index)
        {
            (void)snprintf(title, STORAGE_CATALOG_NAME_BYTES, "%s", storage_sim_tags[i].Title);
            if (storage_sim_tags[i].Artist != NULL)
            {
                (void)snprintf(artist, STORAGE_CATALOG_NAME_BYTES, "%s", storage_sim_tags[i].Artist);
            }
            break;
        }
    }

    return true;
}

/**
 * @brief 复位为已挂载、第 1 代、固定曲库非空，窗口槽与名称槽 IDLE。
 * @note  须在 gui_music_init() 之前调用。
 */
void storage_sim_init(void)
{
    storage_listbuffer_bind(NULL);
    storage_namebuffer_bind(NULL);
    storage_sim_mounted = true;
    storage_sim_empty = false;
    storage_sim_generation = 1U;
    (void)memset(storage_sim_name_fail, 0, sizeof(storage_sim_name_fail));
    storage_sim_name_delay = 0U;
    storage_sim_name_waited = 0U;
}

/**
 * @brief 代替 Storage Task 走一圈：上一圈留下的 PENDING 窗口与名字在本圈填成 READY。
 * @note  每圈在 gui_music_step() 之前调用，因此 request 恰好在下一圈可 APPLY。
 *        名称槽与生产一样在未挂载时也应答（代次 0）；设了应答延迟时 PENDING
 *        多等 storage_sim_name_delay 圈再填。
 */
void storage_sim_step(void)
{
    if (storage_sim_mounted)
    {
        (void)storage_listbuffer_load();
    }

    if (storage_namebuffer.Status != STORAGE_NAMEBUFFER_PENDING)
    {
        storage_sim_name_waited = 0U;
        return;
    }

    if (storage_sim_name_waited < storage_sim_name_delay)
    {
        storage_sim_name_waited++;
        return;
    }

    storage_sim_name_waited = 0U;
    storage_namebuffer_load(storage_sim_mounted);
}

/**
 * @brief C 键：在拔卡与重新挂载之间切换，并把代次 +1。
 * @return 切换后是否已挂载；为假时调用方应让假播放后端按拔卡停止。
 * @note  拔卡时把窗口槽与名称槽的 PENDING 打成空 READY。
 */
bool storage_sim_toggle_mount(void)
{
    storage_sim_generation++;
    if (storage_sim_mounted)
    {
        storage_sim_mounted = false;
        storage_listbuffer_complete_unavailable();
        storage_namebuffer_complete_unavailable();
        printf("[storage] card removed (generation %u)\n", (unsigned)storage_sim_generation);
    }
    else
    {
        storage_sim_mounted = true;
        printf("[storage] mounted (generation %u)\n", (unsigned)storage_sim_generation);
    }

    return storage_sim_mounted;
}

/**
 * @brief G 键：模拟重扫，代次 +1。
 * @note  已显示的 Queue 窗口不主动作废，保留到音乐分区下一次 request，用于制造
 *        过期窗口。未挂载时忽略。
 */
void storage_sim_rescan(void)
{
    if (!storage_sim_mounted)
    {
        return;
    }

    storage_sim_generation++;
    printf("[storage] rescan (generation %u)\n", (unsigned)storage_sim_generation);
}

/**
 * @brief E 键：模拟重扫得到空库（再按一次恢复固定曲库），代次 +1。
 * @note  未挂载时忽略。已显示的 Queue 窗口与 G 键一样保留到下一次 request。
 */
void storage_sim_toggle_empty(void)
{
    if (!storage_sim_mounted)
    {
        return;
    }

    storage_sim_empty = !storage_sim_empty;
    storage_sim_generation++;
    printf("[storage] rescan %s (generation %u)\n",
           storage_sim_empty ? "empty" : "restored",
           (unsigned)storage_sim_generation);
}

/**
 * @brief 脚本 --name-fail：切换第 index 首的名字是否取不到。
 * @param[in] index 播放列表位置；超出固定曲库时忽略。
 * @note  取不到时按生产防御分支应答代次 0 空名，并打印与生产 WARN 同形的一行。
 */
void storage_sim_toggle_name_fail(uint16_t index)
{
    if (index >= STORAGE_SIM_TITLE_COUNT)
    {
        return;
    }

    storage_sim_name_fail[index] = !storage_sim_name_fail[index];
    printf("[storage] name of idx=%u %s\n",
           (unsigned)index,
           storage_sim_name_fail[index] ? "unavailable" : "available");
}

/**
 * @brief 脚本 --name-delay：名称槽 PENDING 后多等 loops 圈再应答。
 * @param[in] loops 额外等待的圈数；0 恢复为下一圈应答。
 */
void storage_sim_set_name_delay(uint32_t loops)
{
    storage_sim_name_delay = loops;
    printf("[storage] name reply delay %u loop(s)\n", (unsigned)loops);
}

/**
 * @brief 当前曲库代次，供假播放后端判定开启是否过期（STALE）。
 * @return 当前代次；从 1 开始，挂载切换与重扫各 +1。
 */
uint32_t storage_sim_generation_get(void)
{
    return storage_sim_generation;
}

/**
 * @brief 当前曲库的曲目数，供假播放后端判定越界与环形步进。
 * @return 曲目数；空库为 0。
 */
uint16_t storage_sim_track_count(void)
{
    return storage_sim_empty ? 0U : STORAGE_SIM_TITLE_COUNT;
}
/**
 * @brief 复位窗口槽；模拟器没有 Storage Task 句柄，不发通知。
 * @param[in] storage_task 忽略。
 */
void storage_listbuffer_bind(void *storage_task)
{
    (void)storage_task;
    storage_listbuffer.Status = STORAGE_LISTBUFFER_IDLE;
    storage_listbuffer.Generation = 0U;
    storage_listbuffer.Index = 0U;
    storage_listbuffer.Length = 0U;
}

/**
 * @brief 同生产：SD 就绪、条数合法且槽位 IDLE 时写入 PENDING。
 */
Storage_StatusTypeDef storage_listbuffer_request(uint16_t index_offset,
                                                 uint16_t index_num,
                                                 uint32_t generation)
{
    if (!storage_task_sd_is_ready() ||
        (index_num == 0U) || (index_num > STORAGE_LISTBUFFER_MAX_ENTRIES) ||
        (storage_listbuffer.Status != STORAGE_LISTBUFFER_IDLE))
    {
        return STORAGE_ERROR;
    }

    storage_listbuffer.Index = index_offset;
    storage_listbuffer.Length = index_num;
    storage_listbuffer.Generation = generation;
    storage_listbuffer.Status = STORAGE_LISTBUFFER_PENDING;
    return STORAGE_OK;
}

/**
 * @brief 同生产：PENDING 时按当前代次从固定曲目填整窗 {曲目显示名, 歌手} 并打 READY。
 * @return 总是 STORAGE_OK（槽位不是 PENDING 时无事可做）。
 * @note  请求代次非 0 且与当前代次不符时填空窗。不做逐行延迟：生产窗口填满前保持 PENDING，
 *        GUI 只见整窗，一次填完与逐圈一行在 GUI 侧不可区分；逐圈排程进板级验收。
 */
Storage_StatusTypeDef storage_listbuffer_load(void)
{
    uint16_t filled = 0U;
    uint16_t i;

    if (storage_listbuffer.Status != STORAGE_LISTBUFFER_PENDING)
    {
        return STORAGE_OK;
    }

    if ((storage_listbuffer.Generation != 0U) &&
        (storage_listbuffer.Generation != storage_sim_generation))
    {
        storage_listbuffer.Length = 0U;
        storage_listbuffer.Generation = 0U;
        storage_listbuffer.Status = STORAGE_LISTBUFFER_READY;
        return STORAGE_OK;
    }

    for (i = 0U;
         (i < storage_listbuffer.Length) &&
         ((uint32_t)storage_listbuffer.Index + i < storage_sim_track_count());
         i++)
    {
        /* 同生产：文件主名也提取不出时该行两列为空串，不中断整窗。 */
        (void)storage_sim_track_text((uint16_t)(storage_listbuffer.Index + i),
                                     storage_listbuffer.Rows[i].Title,
                                     storage_listbuffer.Rows[i].Artist);
        filled++;
    }

    storage_listbuffer.Length = filled;
    storage_listbuffer.Generation = storage_sim_generation;
    storage_listbuffer.Status = STORAGE_LISTBUFFER_READY;
    return STORAGE_OK;
}

/**
 * @brief 同生产：仍为 PENDING 时打成空窗 READY。
 */
void storage_listbuffer_complete_unavailable(void)
{
    if (storage_listbuffer.Status != STORAGE_LISTBUFFER_PENDING)
    {
        return;
    }

    storage_listbuffer.Length = 0U;
    storage_listbuffer.Generation = 0U;
    storage_listbuffer.Status = STORAGE_LISTBUFFER_READY;
}

/**
 * @brief 复位名称槽；模拟器没有 Storage Task 句柄，不发通知。
 * @param[in] storage_task 忽略。
 */
void storage_namebuffer_bind(void *storage_task)
{
    (void)storage_task;
    storage_namebuffer.Status = STORAGE_NAMEBUFFER_IDLE;
    storage_namebuffer.Generation = 0U;
    storage_namebuffer.Index = 0U;
    storage_namebuffer.Title[0] = '\0';
    storage_namebuffer.Artist[0] = '\0';
}

/**
 * @brief 同生产：槽位 IDLE 时写入 PENDING；不检查 SD 是否就绪。
 * @param[in] index 播放列表位置。
 * @param[in] generation 请求代次；0 表示当前。
 * @return STORAGE_OK 已写入 PENDING；槽位不是 IDLE 时 STORAGE_ERROR。
 */
Storage_StatusTypeDef storage_namebuffer_request(uint16_t index, uint32_t generation)
{
    if (storage_namebuffer.Status != STORAGE_NAMEBUFFER_IDLE)
    {
        return STORAGE_ERROR;
    }

    storage_namebuffer.Index = index;
    storage_namebuffer.Generation = generation;
    storage_namebuffer.Status = STORAGE_NAMEBUFFER_PENDING;
    return STORAGE_OK;
}

/**
 * @brief 把名称槽打成代次 0 空名 READY。
 */
static void storage_sim_name_complete_empty(void)
{
    storage_namebuffer.Title[0] = '\0';
    storage_namebuffer.Artist[0] = '\0';
    storage_namebuffer.Generation = 0U;
    storage_namebuffer.Status = STORAGE_NAMEBUFFER_READY;
}

/**
 * @brief 打印与生产 WARN 同形的一行 `Name fail idx=N gen=G`（G 为请求代次）。
 */
static void storage_sim_name_fail_report(void)
{
    printf("[storage] Name fail idx=%u gen=%u\n",
           (unsigned)storage_namebuffer.Index,
           (unsigned)storage_namebuffer.Generation);
}

/**
 * @brief 同生产：PENDING 时按当前代次给出固定曲库的曲目显示名与歌手并打 READY。
 * @param[in] sd_ready 卷是否已挂载；为假时应答代次 0 空名。
 * @note  未挂载或代次不符应答代次 0 空名，不记日志。代次相符（或请求为“当前”）仍越界/提取失败、
 *        以及 --name-fail 标记的位置走生产的防御分支：应答代次 0 空名，并打印与生产 WARN 同形的一行
 *        （请求为“当前”的越界是空库开播的正常过程，不记）。
 */
void storage_namebuffer_load(bool sd_ready)
{
    if (storage_namebuffer.Status != STORAGE_NAMEBUFFER_PENDING)
    {
        return;
    }

    if (!sd_ready ||
        ((storage_namebuffer.Generation != 0U) &&
         (storage_namebuffer.Generation != storage_sim_generation)))
    {
        storage_sim_name_complete_empty();
        return;
    }

    /* 同生产：请求为“当前”时越界是空库开播的正常过程，不记；代次相符的越界与提取失败才是防御分支。 */
    if (storage_namebuffer.Index >= storage_sim_track_count())
    {
        if (storage_namebuffer.Generation != 0U)
        {
            storage_sim_name_fail_report();
        }
        storage_sim_name_complete_empty();
        return;
    }

    /* --name-fail 与文件主名提取失败都是生产的防御分支：应答代次 0 空名并打印同形 WARN。 */
    if (storage_sim_name_fail[storage_namebuffer.Index] ||
        !storage_sim_track_text(storage_namebuffer.Index,
                                storage_namebuffer.Title,
                                storage_namebuffer.Artist))
    {
        storage_sim_name_fail_report();
        storage_sim_name_complete_empty();
        return;
    }

    storage_namebuffer.Generation = storage_sim_generation;
    storage_namebuffer.Status = STORAGE_NAMEBUFFER_READY;
}

/**
 * @brief 同生产：仍为 PENDING 时打成代次 0 空名 READY。
 */
void storage_namebuffer_complete_unavailable(void)
{
    if (storage_namebuffer.Status != STORAGE_NAMEBUFFER_PENDING)
    {
        return;
    }

    storage_sim_name_complete_empty();
}

/**
 * @brief 同生产头：SD 是否就绪；替身无消抖，已挂载即就绪。
 * @return 已挂载为真。
 */
bool storage_task_sd_is_ready(void)
{
    return storage_sim_mounted;
}

/**
 * @brief 同生产头：卷是否已挂载。
 * @return 已挂载为真。
 */
bool storage_task_sd_is_mounted(void)
{
    return storage_sim_mounted;
}
