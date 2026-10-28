/**
  ******************************************************************************
  * @file    gui_service_main_title.c
  * @brief   Now Playing 曲名与歌手 Label：一次写入同一曲目身份的两列文本。
  *
  * @details
  *          本 Module 不认识“名字 / 空白 / `--` / `Unknown Artist`”的语义，只负责画调用方
  *          给出的 UTF-8 文本。两个 Label 的对象、位置、字体、颜色、长文本模式（歌手）与
  *          滚动速度（曲名）由 view/ 决定。曲名每次文本变化后按 Label 当前字体量出宽度：
  *          宽于可见窗口用 LV_LABEL_LONG_SCROLL_CIRCULAR 单行循环滚动，否则
  *          LV_LABEL_LONG_CLIP 静止居中，不换行、不截断、不加省略号；歌手由 view 设为
  *          LV_LABEL_LONG_DOT，超宽截断加省略号（GUI-26）。
  ******************************************************************************
  */

#include "Service/gui/main/title/gui_service_main_title.h"
#include "Service/gui/gui_service_config.h"

#include <string.h>

#include "Service/gui/view/gui_service_view.h"
#include "lvgl.h"

/** @brief 无当前曲或取不到名字时调用方给出的文本；prepare 先写它作为初始显示。 */
#define SERVICE_GUI_MAIN_TITLE_INITIAL  "--"

/** @brief 曲名 Label，prepare 时取得。 */
static lv_obj_t *service_gui_main_title_label;
/** @brief 歌手 Label，prepare 时取得。 */
static lv_obj_t *service_gui_main_title_artist_label;

/**
 * @brief 取得曲名与歌手 Label，写入初始 `--` 与空歌手。
 * @retval SERVICE_OK 已取得句柄并写入。
 * @retval SERVICE_NOT_READY 曲名或歌手 Label 尚未创建。
 * @note  仅由 service_gui_main_prepare() 调用一次。
 */
Service_StatusTypeDef service_gui_main_title_prepare(void)
{
    const Service_GUI_ViewMusicTypeDef *music = &service_gui_view_get()->music;

    service_gui_main_title_label = music->title_label;
    service_gui_main_title_artist_label = music->artist_label;
    if ((service_gui_main_title_label == NULL) || (service_gui_main_title_artist_label == NULL))
    {
        return SERVICE_NOT_READY;
    }

    lv_label_set_text(service_gui_main_title_label, SERVICE_GUI_MAIN_TITLE_INITIAL);
    lv_label_set_text(service_gui_main_title_artist_label, "");
    return SERVICE_OK;
}

/**
 * @brief 写曲名：按像素宽度选择静止居中或循环滚动。
 * @param[in] label 曲名 Label。
 * @param[in] text 以 '\0' 结尾的 UTF-8 文本。
 * @note  文本相同时不重写，避免重启滚动动画。先设长文本模式再写文本：切换模式会
 *        删除旧滚动动画并复位偏移，写文本后 LVGL 按新模式重建动画。
 */
static void service_gui_main_title_set_title(lv_obj_t *label, const char *text)
{
    lv_coord_t width;

    if (strcmp(lv_label_get_text(label), text) == 0)
    {
        return;
    }

    width = lv_txt_get_width(text,
                             (uint32_t)strlen(text),
                             lv_obj_get_style_text_font(label, LV_PART_MAIN),
                             lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
                             LV_TEXT_FLAG_NONE);
    lv_label_set_long_mode(label,
                           (width > SERVICE_GUI_MUSIC_TITLE_WIDTH) ?
                               LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text);
}

/**
 * @brief 同时显示曲名与歌手两列文本。
 * @param[in] text 曲名，以 '\0' 结尾的 UTF-8 文本，Label 会拷贝；空串表示留空。
 * @param[in] artist 歌手，以 '\0' 结尾的 UTF-8 文本，Label 会拷贝；NULL 视同空串。
 * @retval SERVICE_OK 两列已写入，或与 Label 现有文本相同未重写。
 * @retval SERVICE_INVALID_PARAM text 为 NULL；两列都不改。
 * @retval SERVICE_NOT_READY 曲名或歌手 Label 尚未创建。
 * @note  两列在同一次调用里写完，下一次刷新同时出现。歌手文本相同也不重写。
 */
Service_StatusTypeDef service_gui_main_title_apply(const char *text, const char *artist)
{
    const char *artist_text = (artist == NULL) ? "" : artist;

    if (text == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }

    if ((service_gui_main_title_label == NULL) || (service_gui_main_title_artist_label == NULL))
    {
        return SERVICE_NOT_READY;
    }

    service_gui_main_title_set_title(service_gui_main_title_label, text);
    if (strcmp(lv_label_get_text(service_gui_main_title_artist_label), artist_text) != 0)
    {
        lv_label_set_text(service_gui_main_title_artist_label, artist_text);
    }
    return SERVICE_OK;
}
