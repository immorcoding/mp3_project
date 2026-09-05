/**
  ******************************************************************************
  * @file    gui_service_main_queue.c
  * @brief   按 Length 用 SquareLine 范本构造生成 Queue 可见行。
  *
  * @details
  *          构造序列从 GUI/screens/ui_Main.c 的 SongPanel1 范本摘出，for 循环
  *          写入 QueueTab。不对手上的 LVGL 对象做样式拷贝。SquareLine 导出的
  *          那一行隐藏，避免和循环行叠在一起。当前/非当前只改 Border Opa、
  *          曲名色、Long mode 和右侧符号 Opa，不改 Border Width。假数据填
  *          Buffer[i]；不得包含 storage_listbuffer.h。
  ******************************************************************************
  */

#include "Service/gui/main/queue/gui_service_main_queue.h"
#include "Service/gui/main/queue/gui_service_main_queue_config.h"

#include <stdbool.h>
#include <stdint.h>

#include "GUI/ui.h"

/**
 * @brief 已生成到 QueueTab 的一行对象。
 */
typedef struct
{
    lv_obj_t *panel;    /**< 行根 Panel。 */
    lv_obj_t *name;     /**< 曲名 Label，对应 Buffer[i]。 */
    lv_obj_t *creator;  /**< 歌手 Label；真数据接入前用假数据。 */
    lv_obj_t *status;   /**< 右侧符号。 */
} Service_GUI_MainQueueRowTypeDef;

/**
 * @brief 本场假窗口的一首：曲名站位 Buffer[i]，歌手站位尚未落地的元数据。
 */
typedef struct
{
    const char *title;   /**< 曲名，对应 READY 后的 Buffer[i]。 */
    const char *artist;  /**< 歌手假数据。 */
} Service_GUI_MainQueueFakeTrackTypeDef;

/** @brief 游标未落地前的假窗口内容；Length 取本表条数。 */
static const Service_GUI_MainQueueFakeTrackTypeDef
    service_gui_main_queue_fake_tracks[] =
{
    { "Night Drive On The Glass Harbor", "Aurora Lane" },
    { "Glass Harbor", "North Station" },
    { "Quiet Motors", "Low Tide" },
    { "Indigo Room", "Paper Birds" },
};

/** @brief 已生成的可见行；未使用槽位保持 NULL。 */
static Service_GUI_MainQueueRowTypeDef service_gui_main_queue_rows[
    SERVICE_GUI_MAIN_QUEUE_MAX_ROWS];

/**
 * @brief 按 SquareLine 范本构造一行，父对象为 QueueTab。
 * @param[out] row 新行对象指针。
 * @retval SERVICE_OK 已按范本构造。
 * @retval SERVICE_ERROR LVGL 未能创建对象。
 * @note 须与 GUI/screens/ui_Main.c 中 SongPanel1 及其子对象的构造保持同步；
 *       SquareLine 重新导出后对照更新本函数，不得手改 GUI/。
 */
static Service_StatusTypeDef service_gui_main_queue_create_row(
    Service_GUI_MainQueueRowTypeDef *row)
{
    lv_obj_t *panel;
    lv_obj_t *info;
    lv_obj_t *name;
    lv_obj_t *creator;
    lv_obj_t *status;

    panel = lv_obj_create(ui_QueueTab);
    if (panel == NULL)
    {
        return SERVICE_ERROR;
    }

    lv_obj_set_width(panel, lv_pct(100));
    lv_obj_set_height(panel, lv_pct(25));
    lv_obj_set_align(panel, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(panel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xE7E7E7), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_object_set_themeable_style_property(panel, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR,
                                           _ui_theme_color_Blue1);
    ui_object_set_themeable_style_property(panel, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA,
                                           _ui_theme_alpha_Blue1);
    lv_obj_set_style_border_width(panel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(panel, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(panel, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(panel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(panel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xE7E7E7), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(panel, 80, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(panel, lv_color_hex(0xF1F6FF), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_opa(panel, 80, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(panel, 1, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_pad(panel, 0, LV_PART_MAIN | LV_STATE_PRESSED);

    info = lv_obj_create(panel);
    if (info == NULL)
    {
        return SERVICE_ERROR;
    }

    lv_obj_remove_style_all(info);
    lv_obj_set_width(info, lv_pct(80));
    lv_obj_set_height(info, lv_pct(100));
    lv_obj_set_align(info, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    name = lv_label_create(info);
    creator = lv_label_create(info);
    status = lv_label_create(panel);
    if ((name == NULL) || (creator == NULL) || (status == NULL))
    {
        return SERVICE_ERROR;
    }

    lv_obj_set_width(name, lv_pct(100));
    lv_obj_set_height(name, LV_SIZE_CONTENT);
    lv_obj_set_align(name, LV_ALIGN_CENTER);
    lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_clear_flag(name, LV_OBJ_FLAG_SCROLLABLE);
    ui_object_set_themeable_style_property(name, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR,
                                           _ui_theme_color_Blue1);
    ui_object_set_themeable_style_property(name, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA,
                                           _ui_theme_alpha_Blue1);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_width(creator, lv_pct(100));
    lv_obj_set_height(creator, LV_SIZE_CONTENT);
    lv_obj_set_align(creator, LV_ALIGN_CENTER);
    lv_label_set_long_mode(creator, LV_LABEL_LONG_DOT);
    lv_obj_clear_flag(creator, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_color(creator, lv_color_hex(0xF1F6FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(creator, 180, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(creator, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_width(status, LV_SIZE_CONTENT);
    lv_obj_set_height(status, LV_SIZE_CONTENT);
    lv_obj_set_align(status, LV_ALIGN_CENTER);
    lv_label_set_text(status, "S");
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);
    ui_object_set_themeable_style_property(status, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR,
                                           _ui_theme_color_Blue1);
    ui_object_set_themeable_style_property(status, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA,
                                           _ui_theme_alpha_Blue1);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    row->panel = panel;
    row->name = name;
    row->creator = creator;
    row->status = status;

    return SERVICE_OK;
}

/**
 * @brief 非当前行把右侧符号做成透明，但仍留在 Flex 里占位。
 * @param[in,out] status 行内 SongStatus Label。
 * @param[in] visible 当前行可见，非当前行透明。
 * @note 不得使用 HIDDEN：范本信息组宽度是 80%，Panel 是 SPACE_BETWEEN，拿掉
 *       右侧符号会把文字组拉开，左侧多出一块空。
 */
static void service_gui_main_queue_set_status_visible(
    lv_obj_t *status,
    bool visible)
{
    lv_obj_clear_flag(status, LV_OBJ_FLAG_HIDDEN);

    if (visible)
    {
        ui_object_set_themeable_style_property(
            status,
            LV_PART_MAIN | LV_STATE_DEFAULT,
            LV_STYLE_TEXT_OPA,
            _ui_theme_alpha_Blue1);
    }
    else
    {
        lv_obj_set_style_text_opa(
            status,
            LV_OPA_TRANSP,
            LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

/**
 * @brief 当前行显示左边条，非当前行只把 Border Opa 打到 0。
 * @param[in,out] panel 行根 Panel。
 * @param[in] visible 当前行可见。
 * @note 不改 Border Width / Side，避免文字漂移；与 PRESSED 不改 Border Width 同一条。
 */
static void service_gui_main_queue_set_current_border_visible(
    lv_obj_t *panel,
    bool visible)
{
    if (visible)
    {
        ui_object_set_themeable_style_property(
            panel,
            LV_PART_MAIN | LV_STATE_DEFAULT,
            LV_STYLE_BORDER_OPA,
            _ui_theme_alpha_Blue1);
    }
    else
    {
        lv_obj_set_style_border_opa(
            panel,
            LV_OPA_TRANSP,
            LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

/**
 * @brief 填写一行文字并套用当前/非当前样式。
 * @param[in,out] row 已按范本构造的行。
 * @param[in] title 曲名，对应 Buffer[i]。
 * @param[in] artist 歌手文本。
 * @param[in] is_current 是否为窗口内正在播放的那一行。
 * @note 只改文字、Border Opa、符号 Opa、曲名色和 Long mode；不改 Border Width。
 */
static void service_gui_main_queue_apply_row(
    Service_GUI_MainQueueRowTypeDef *row,
    const char *title,
    const char *artist,
    bool is_current)
{
    lv_label_set_text(row->name, title);
    lv_label_set_text(row->creator, artist);
    service_gui_main_queue_set_current_border_visible(row->panel, is_current);
    service_gui_main_queue_set_status_visible(row->status, is_current);

    if (is_current)
    {
        ui_object_set_themeable_style_property(
            row->name,
            LV_PART_MAIN | LV_STATE_DEFAULT,
            LV_STYLE_TEXT_COLOR,
            _ui_theme_color_Blue1);
        lv_label_set_long_mode(row->name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
    else
    {
        ui_object_set_themeable_style_property(
            row->name,
            LV_PART_MAIN | LV_STATE_DEFAULT,
            LV_STYLE_TEXT_COLOR,
            _ui_theme_color_White1);
        lv_label_set_long_mode(row->name, LV_LABEL_LONG_DOT);
    }
}

/**
 * @brief 隐藏 SquareLine 单行范本，避免它占掉 QueueTab 的一个 Flex 槽。
 * @note 范本仍由 ui_init() 创建，本 Module 不删除、不改 GUI/。
 */
static void service_gui_main_queue_hide_template(void)
{
    lv_obj_add_flag(ui_SongPanel1, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief 按假数据 Length 用范本构造生成 Queue 可见行。
 * @retval SERVICE_OK 已按 Length 生成行，或 Length 为 0 已隐藏范本。
 * @retval SERVICE_NOT_READY QueueTab 或范本尚未导出。
 * @retval SERVICE_ERROR 构造行时 LVGL 未能创建对象。
 * @note 必须在 ui_init() 之后、Boot 占用 Canvas 之前由 Main 编排入口调用。
 *       竖向滚动交给 QueueTab；不打开 Tabview 内部 Content 的滚动。
 */
Service_StatusTypeDef service_gui_main_queue_prepare(void)
{
    Service_StatusTypeDef status;
    uint16_t length;
    uint16_t i;
    uint16_t fake_count;

    if ((ui_QueueTab == NULL) || (ui_SongPanel1 == NULL))
    {
        return SERVICE_NOT_READY;
    }

    fake_count = (uint16_t)(
        sizeof(service_gui_main_queue_fake_tracks) /
        sizeof(service_gui_main_queue_fake_tracks[0]));
    length = fake_count;
    if (length > SERVICE_GUI_MAIN_QUEUE_MAX_ROWS)
    {
        length = SERVICE_GUI_MAIN_QUEUE_MAX_ROWS;
    }

    lv_obj_add_flag(ui_QueueTab, LV_OBJ_FLAG_SCROLLABLE);
    service_gui_main_queue_hide_template();

    if (length == 0U)
    {
        return SERVICE_OK;
    }

    for (i = 0U; i < length; i++)
    {
        status = service_gui_main_queue_create_row(
            &service_gui_main_queue_rows[i]);
        if (status != SERVICE_OK)
        {
            return status;
        }

        service_gui_main_queue_apply_row(
            &service_gui_main_queue_rows[i],
            service_gui_main_queue_fake_tracks[i].title,
            service_gui_main_queue_fake_tracks[i].artist,
            (i == 0U));
    }

    return SERVICE_OK;
}
