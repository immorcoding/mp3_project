/**
  ******************************************************************************
  * @file    gui_music_title.h
  * @brief   音乐分区私有的曲名客户：按已追上的曲目身份写曲名需求，按需求序号采信结果，决定曲名区显示什么。
  *
  * @details
  *          纯 C，不依赖 LVGL、FreeRTOS 与 Storage 头；与 Queue 窗口客户并列。GUI 不等文件：身份变了就把最新需求（带递增序号）
  *          写进需求格，前台任务答在结果格，结果带回所答的需求序号，客户只采信序号等于最新需求的结果（曲名要比序号，
  *          窗口不比，#200）。输出本帧要不要写需求、对结果的动作，以及曲名区的三种显示（名字 / 空白 / `--`）和本帧要不要
  *          Apply、Apply 哪一对曲名与歌手。文本本身留在结果格的 GUI 副本里，本模块只交出指针。
  *          拿不到锁（需求格写失败）时需求保持“待写”，下一帧再试。
  ******************************************************************************
  */

#ifndef GUI_MUSIC_TITLE_H
#define GUI_MUSIC_TITLE_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/apps/music/music_cells.h"

/** @brief 曲目身份：与 Queue 高亮、唱盘归零同源，取自快照归约已追上的快照。 */
typedef struct
{
    bool has_track;      /**< 是否有当前曲；为假时其余字段无意义。 */
    uint32_t generation; /**< 曲库代次；0 表示开启尚未确认（写需求时表示“当前”）。 */
    uint16_t index;      /**< 播放列表位置。 */
} GUI_MusicTitleIdentityTypeDef;

/** @brief 曲名区应显示的内容。 */
typedef enum
{
    GUI_MUSIC_TITLE_SHOW_NONE = 0U, /**< `--`：无当前曲，或有当前曲但取不到名字。 */
    GUI_MUSIC_TITLE_SHOW_BLANK,     /**< 空白：已采信新身份，结果未到。 */
    GUI_MUSIC_TITLE_SHOW_NAME       /**< 名字：本帧 ACCEPT 时由调用方从结果副本里取。 */
} GUI_MusicTitleShowTypeDef;

/** @brief 本帧对读到的新结果的动作。 */
typedef enum
{
    GUI_MUSIC_TITLE_ACTION_NONE = 0U, /**< 本帧没有新结果。 */
    GUI_MUSIC_TITLE_ACTION_ACCEPT,    /**< 结果属于当前身份且有名字：显示结果副本里的名字。 */
    GUI_MUSIC_TITLE_ACTION_DISCARD    /**< 结果已过期（所答的需求已被覆盖）或代次 0：不读名字。 */
} GUI_MusicTitleActionTypeDef;

/** @brief 一次 TitleApply 的两列文本；两者属于同一次结果的曲目身份（GUI-26）。 */
typedef struct
{
    const char *title;  /**< 曲名：结果里的名字、空串（留空）或 `--`。 */
    const char *artist; /**< 歌手：结果里的歌手、`Unknown Artist`，或空串（曲名为空白或 `--`）。 */
} GUI_MusicTitleTextTypeDef;

/**
 * @brief 曲名客户状态。
 * @note  字段只供 gui_music_title.c 使用；调用方不直接读写。
 */
typedef struct
{
    bool has_identity;                      /**< 已采信过至少一个身份。 */
    GUI_MusicTitleIdentityTypeDef identity; /**< 最近采信的身份。 */
    uint32_t sequence;                      /**< 已写出的最新需求序号；0 表示还没写过。 */
    uint32_t request_generation;            /**< 最新需求写的代次（0 表示“当前”）；冷启动确认时与采信的身份代次不同。 */
    bool write_pending;                     /**< 需要写（或重写）需求：新身份、写失败、结果过期要重写。 */
    bool reply_open;                        /**< 已写出的最新需求仍然适用于当前身份：只有这时序号相符的结果才被采信。 */
    bool answered;                          /**< 已处理过所答该需求的结果。 */
    GUI_MusicTitleShowTypeDef shown;        /**< 曲名区应显示的内容。 */
    uint32_t name_generation;               /**< 已显示名字的结果代次；冷启动 0 → G 时据此决定保留。 */
    bool has_applied;                       /**< 已交出过至少一次 Apply。 */
    GUI_MusicTitleShowTypeDef applied;      /**< 最近一次交出的显示；空白与 `--` 只在与它不同时再交出。 */
} GUI_MusicTitleClientTypeDef;

void gui_music_title_client_init(GUI_MusicTitleClientTypeDef *client);
GUI_MusicTitleActionTypeDef gui_music_title_poll(
    GUI_MusicTitleClientTypeDef *client,
    const GUI_MusicTitleIdentityTypeDef *identity,
    const MusicCells_TitleResultTypeDef *reply);
GUI_MusicTitleShowTypeDef gui_music_title_shown(const GUI_MusicTitleClientTypeDef *client);
bool gui_music_title_request_due(const GUI_MusicTitleClientTypeDef *client);
MusicCells_TitleRequestTypeDef gui_music_title_request(const GUI_MusicTitleClientTypeDef *client);
void gui_music_title_request_sent(GUI_MusicTitleClientTypeDef *client);
bool gui_music_title_text(
    GUI_MusicTitleClientTypeDef *client,
    GUI_MusicTitleActionTypeDef action,
    const char *reply_title,
    const char *reply_artist,
    GUI_MusicTitleTextTypeDef *text);

#endif /* GUI_MUSIC_TITLE_H */
