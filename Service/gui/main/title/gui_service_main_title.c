/**
  ******************************************************************************
  * @file    gui_service_main_title.c
  * @brief   Now Playing 曲名 Label：写入调用方给出的文本，按像素宽度选择静止居中或循环滚动。
  *
  * @details
  *          本 Module 不认识“名字 / 空白 / `--`”的语义，只负责画调用方给出的 UTF-8 文本。
  *          Label 对象、位置、字体、颜色与滚动速度由 view/ 决定；这里每次文本变化后按
  *          Label 当前字体量出文本宽度：宽于可见窗口用 LV_LABEL_LONG_SCROLL_CIRCULAR
  *          单行循环滚动，否则 LV_LABEL_LONG_CLIP 静止居中，不换行、不截断、不加省略号。
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

/**
 * @brief 取得曲名 Label 并写入初始 `--`。
 * @retval SERVICE_OK 已取得句柄并写入。
 * @retval SERVICE_NOT_READY 曲名 Label 尚未创建。
 * @note  仅由 service_gui_main_prepare() 调用一次。
 */
Service_StatusTypeDef service_gui_main_title_prepare(void)
{
    service_gui_main_title_label = service_gui_view_get()->music.title_label;
    if (service_gui_main_title_label == NULL)
    {
        return SERVICE_NOT_READY;
    }

    lv_label_set_text(service_gui_main_title_label, SERVICE_GUI_MAIN_TITLE_INITIAL);
    return SERVICE_OK;
}

/**
 * @brief 显示一段曲名文本，并按像素宽度选择静止居中或循环滚动。
 * @param[in] text 以 '\0' 结尾的 UTF-8 文本，Label 会拷贝；空串表示留空。
 * @retval SERVICE_OK 已写入，或与 Label 现有文本相同未重写。
 * @retval SERVICE_INVALID_PARAM text 为 NULL。
 * @retval SERVICE_NOT_READY 曲名 Label 尚未创建。
 * @note  文本相同时不重写，避免重启滚动动画。先设长文本模式再写文本：切换模式会
 *        删除旧滚动动画并复位偏移，写文本后 LVGL 按新模式重建动画。
 */
Service_StatusTypeDef service_gui_main_title_apply(const char *text)
{
    lv_obj_t *label = service_gui_main_title_label;
    lv_coord_t width;

    if (text == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }

    if (label == NULL)
    {
        return SERVICE_NOT_READY;
    }

    if (strcmp(lv_label_get_text(label), text) == 0)
    {
        return SERVICE_OK;
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
    return SERVICE_OK;
}
