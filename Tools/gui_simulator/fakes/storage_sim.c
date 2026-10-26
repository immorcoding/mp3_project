/**
  ******************************************************************************
  * @file    storage_sim.c
  * @brief   Storage 替身：只替换音乐分区用到的窗口槽、名称槽与 SD 状态。
  *
  * @details
  *          按生产头实现 `storage_listbuffer` / `storage_namebuffer` 全局对象、
  *          `storage_listbuffer_*`、`storage_namebuffer_*` 与
  *          `storage_task_sd_is_ready/mounted()`。曲库是 11 首固定曲目：原 10 首英文
  *          曲目（与旧演示分区一致）加一首需要滚动的超长英文名；`E` 键可把曲库换成空库。
  *          固定曲库没有路径，窗口行与曲目显示名都直接给出曲名。没有 Storage Task：
  *          sim_main.c 每圈先调 storage_sim_step()，上一圈 request 的窗口与名字在本圈填成 READY。
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
#include "APP/tasks/storage/catalog/storage_listbuffer.h"
#include "APP/tasks/storage/catalog/storage_namebuffer.h"

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
    "An Unusually Long English Track Name That Has To Scroll Across Now Playing",
};

#define STORAGE_SIM_TITLE_COUNT \
    ((uint16_t)(sizeof(storage_sim_titles) / sizeof(storage_sim_titles[0])))

_Static_assert(STORAGE_SIM_TITLE_COUNT <= STORAGE_LISTBUFFER_MAX_ENTRIES,
               "fixed playlist must fit in one listbuffer window");

Storage_ListBufferTypeDef storage_listbuffer;
Storage_NameBufferTypeDef storage_namebuffer;

static bool storage_sim_mounted;
static bool storage_sim_empty;
static uint32_t storage_sim_generation;

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
}

/**
 * @brief 代替 Storage Task 走一圈：上一圈留下的 PENDING 窗口与名字在本圈填成 READY。
 * @note  每圈在 gui_music_step() 之前调用，因此 request 恰好在下一圈可 APPLY。
 *        名称槽与生产一样在未挂载时也应答（代次 0）。
 */
void storage_sim_step(void)
{
    if (storage_sim_mounted)
    {
        (void)storage_listbuffer_load();
    }
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
 * @brief 同生产：PENDING 时按当前代次从固定曲目填整窗并打 READY。
 * @return 总是 STORAGE_OK（槽位不是 PENDING 时无事可做）。
 * @note  请求代次非 0 且与当前代次不符时填空窗。
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

    (void)memset(storage_listbuffer.Buffer, 0, sizeof(storage_listbuffer.Buffer));
    for (i = 0U;
         (i < storage_listbuffer.Length) &&
         ((uint32_t)storage_listbuffer.Index + i < storage_sim_track_count());
         i++)
    {
        (void)snprintf(storage_listbuffer.Buffer[i],
                       STORAGE_LISTBUFFER_PATH_BYTES,
                       "%s",
                       storage_sim_titles[storage_listbuffer.Index + i]);
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
    storage_namebuffer.Buffer[0] = '\0';
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
    storage_namebuffer.Buffer[0] = '\0';
    storage_namebuffer.Generation = 0U;
    storage_namebuffer.Status = STORAGE_NAMEBUFFER_READY;
}

/**
 * @brief 同生产：PENDING 时按当前代次给出固定曲库的曲名并打 READY。
 * @param[in] sd_ready 卷是否已挂载；为假时应答代次 0 空名。
 * @note  代次不符、越界或未挂载应答代次 0 空名。固定曲库不会进入生产的防御分支，不记日志。
 */
void storage_namebuffer_load(bool sd_ready)
{
    if (storage_namebuffer.Status != STORAGE_NAMEBUFFER_PENDING)
    {
        return;
    }

    if (!sd_ready ||
        ((storage_namebuffer.Generation != 0U) &&
         (storage_namebuffer.Generation != storage_sim_generation)) ||
        (storage_namebuffer.Index >= storage_sim_track_count()))
    {
        storage_sim_name_complete_empty();
        return;
    }

    (void)snprintf(storage_namebuffer.Buffer,
                   sizeof(storage_namebuffer.Buffer),
                   "%s",
                   storage_sim_titles[storage_namebuffer.Index]);
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
