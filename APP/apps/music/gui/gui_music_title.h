/**
  ******************************************************************************
  * @file    gui_music_title.h
  * @brief   音乐分区私有的曲名客户：按已追上的曲目身份请求名称槽，决定曲名区显示什么。
  *
  * @details
  *          纯 C，不依赖 LVGL、FreeRTOS 与 Storage 头；与 Queue 窗口客户并列。
  *          输入是快照归约已追上的曲目身份与名称槽的状态、应答代次，输出本圈对槽的
  *          动作、曲名区的三种显示（名字 / 空白 / `--`），以及本圈要不要 Apply、
  *          Apply 哪一对曲名与歌手。文本本身留在名称槽里，本模块只交出指针。
  ******************************************************************************
  */

#ifndef GUI_MUSIC_TITLE_H
#define GUI_MUSIC_TITLE_H

#include <stdbool.h>
#include <stdint.h>

#define GUI_MUSIC_TITLE_IDLE    0U  /* 与 STORAGE_NAMEBUFFER_IDLE 同值。 */
#define GUI_MUSIC_TITLE_PENDING 1U  /* 与 STORAGE_NAMEBUFFER_PENDING 同值。 */
#define GUI_MUSIC_TITLE_READY   2U  /* 与 STORAGE_NAMEBUFFER_READY 同值。 */

/** @brief 曲目身份：与 Queue 高亮、唱盘归零同源，取自快照归约已追上的快照。 */
typedef struct
{
    bool has_track;      /**< 是否有当前曲；为假时其余字段无意义。 */
    uint32_t generation; /**< 曲库代次；0 表示开启尚未确认（请求时表示“当前”）。 */
    uint16_t index;      /**< 播放列表位置。 */
} GUI_MusicTitleIdentityTypeDef;

/** @brief 曲名区应显示的内容。 */
typedef enum
{
    GUI_MUSIC_TITLE_SHOW_NONE = 0U, /**< `--`：无当前曲，或有当前曲但取不到名字。 */
    GUI_MUSIC_TITLE_SHOW_BLANK,     /**< 空白：已采信新身份，应答未到。 */
    GUI_MUSIC_TITLE_SHOW_NAME       /**< 名字：本圈 ACCEPT 时由调用方从槽里拷出。 */
} GUI_MusicTitleShowTypeDef;

/** @brief 本圈对名称槽的动作。 */
typedef enum
{
    GUI_MUSIC_TITLE_ACTION_NONE = 0U, /**< 不碰槽。 */
    GUI_MUSIC_TITLE_ACTION_REQUEST,   /**< 槽 IDLE，按 request_identity() 提交请求。 */
    GUI_MUSIC_TITLE_ACTION_ACCEPT,    /**< 应答属于当前身份且有名字：显示槽内名字，再写回 IDLE。 */
    GUI_MUSIC_TITLE_ACTION_DISCARD    /**< 应答已过期或代次 0：不读名字，直接写回 IDLE。 */
} GUI_MusicTitleActionTypeDef;

/** @brief 一次 TitleApply 的两列文本；两者属于同一次应答的曲目身份（GUI-26）。 */
typedef struct
{
    const char *title;  /**< 曲名：槽内名字、空串（留空）或 `--`。 */
    const char *artist; /**< 歌手：槽内歌手、`Unknown Artist`，或空串（曲名为空白或 `--`）。 */
} GUI_MusicTitleTextTypeDef;

/**
 * @brief 曲名客户状态。
 * @note  字段只供 gui_music_title.c 使用；调用方不直接读写。
 */
typedef struct
{
    bool has_identity;                      /**< 已采信过至少一个身份。 */
    GUI_MusicTitleIdentityTypeDef identity; /**< 最近采信的身份。 */
    bool need_request;                      /**< 当前身份还没有在途或已完成的请求。 */
    bool in_flight;                         /**< 有一次请求尚未消费（槽 PENDING 或 READY）。 */
    GUI_MusicTitleIdentityTypeDef requested; /**< 在途请求所用的身份；应答按它判断是否过期。 */
    GUI_MusicTitleShowTypeDef shown;        /**< 曲名区应显示的内容。 */
    uint32_t name_generation;               /**< 已显示名字的应答代次；冷启动 0 → G 时据此决定保留。 */
    bool has_applied;                       /**< 已交出过至少一次 Apply。 */
    GUI_MusicTitleShowTypeDef applied;      /**< 最近一次交出的显示；空白与 `--` 只在与它不同时再交出。 */
} GUI_MusicTitleClientTypeDef;

void gui_music_title_client_init(GUI_MusicTitleClientTypeDef *client);
GUI_MusicTitleActionTypeDef gui_music_title_poll(
    GUI_MusicTitleClientTypeDef *client,
    const GUI_MusicTitleIdentityTypeDef *identity,
    uint32_t status,
    uint32_t reply_generation);
GUI_MusicTitleShowTypeDef gui_music_title_shown(const GUI_MusicTitleClientTypeDef *client);
GUI_MusicTitleIdentityTypeDef gui_music_title_request_identity(
    const GUI_MusicTitleClientTypeDef *client);
void gui_music_title_request_failed(GUI_MusicTitleClientTypeDef *client);
bool gui_music_title_text(
    GUI_MusicTitleClientTypeDef *client,
    GUI_MusicTitleActionTypeDef action,
    const char *slot_title,
    const char *slot_artist,
    GUI_MusicTitleTextTypeDef *text);

#endif /* GUI_MUSIC_TITLE_H */
