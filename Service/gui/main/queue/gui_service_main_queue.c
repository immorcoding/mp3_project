/**
  ******************************************************************************
  * @file    gui_service_main_queue.c
  * @brief   按 Length 用 SquareLine 范本构造生成 Queue 可见行。
  *
  * @details
 *          构造序列从 GUI/screens/ui_Main.c 的 SongPanel1 范本摘出，for 循环
 *          写入 QueueTab。不对手上的 LVGL 对象做样式拷贝。SquareLine 导出的
 *          那一行隐藏，避免和循环行叠在一起。当前/非当前只改 Border Opa、
 *          曲名色、Long mode 和右侧符号 Opa，不改 Border Width。点按 CLICKED
 *          只刷新行样式并记下播放列表下标，不打开文件。可见行数跟 Apply
 *          传入的 Length 走，至多 SERVICE_GUI_MAIN_QUEUE_MAX_ROWS；曲名是 GUI Task 传入的窗口文本。
 *          窗口滑动时在已有 panel 上转 head 改字，不无限 create。不得包含
 *          storage_listbuffer.h。
  ******************************************************************************
  */

#include "Service/gui/main/queue/gui_service_main_queue.h"
#include "Service/gui/main/queue/gui_service_main_queue_config.h"
#include "Service/gui/gui_service.h"
#include "Service/gui/gui_service_input.h"
#include "Service/gui/theme/gui_service_theme_style.h"

#include <stdbool.h>
#include <stdint.h>

#include "Service/gui/view/gui_service_view.h"

/**
 * @brief 已生成到 QueueTab 的一行对象。
 */
typedef struct
{
    lv_obj_t *panel;    /**< 行根 Panel。 */
    lv_obj_t *name;     /**< 曲名 Label。 */
    lv_obj_t *creator;  /**< 歌手 Label；元数据未落地前为空串。 */
    lv_obj_t *status;   /**< 右侧符号。 */
} Service_GUI_MainQueueRowTypeDef;

/** @brief Music 页对象句柄，prepare 时取得。 */
static const Service_GUI_ViewMusicTypeDef *service_gui_main_queue_music;

/** @brief 已按范本构造的行；未创建槽位保持 NULL。 */
static Service_GUI_MainQueueRowTypeDef service_gui_main_queue_rows[
    SERVICE_GUI_MAIN_QUEUE_MAX_ROWS];

/** @brief 已构造的行数，Apply 只增不毁，多余的 Hidden。 */
static uint16_t service_gui_main_queue_created;

/** @brief 上一窗在播放列表上的起点，供转 head 与滚动补偿。 */
static uint16_t service_gui_main_queue_applied_index;

/** @brief 上一窗实际可见条数，供点击把窗内槽位换成播放列表下标。 */
static uint16_t service_gui_main_queue_applied_length;

static void service_gui_main_queue_on_panel_clicked(lv_event_t *e);

/**
 * @brief 按 SquareLine 范本构造一行，父对象为 QueueTab。
 * @param[out] row 新行对象指针。
 * @retval SERVICE_OK 已按范本构造。
 * @retval SERVICE_ERROR LVGL 未能创建对象；已挂到 QueueTab 的半成品 panel 会删除。
 * @note 须与 GUI/screens/ui_Main.c 中 SongPanel1 及其子对象的构造保持同步；
 *       SquareLine 重新导出后对照更新本函数，不得手改 GUI/。
 *       SongStatus 范本仍是占位 `S`；运行时写 `LV_SYMBOL_AUDIO`（montserrat_14 含该字形）。
 */
static Service_StatusTypeDef service_gui_main_queue_create_row(
    Service_GUI_MainQueueRowTypeDef *row)
{
    lv_obj_t *panel;
    lv_obj_t *info;
    lv_obj_t *name;
    lv_obj_t *creator;
    lv_obj_t *status;

    panel = lv_obj_create(service_gui_main_queue_music->queue_tab);
    if (panel == NULL)
    {
        return SERVICE_ERROR;
    }

    lv_obj_set_width(panel, lv_pct(100));
    lv_obj_set_height(panel, lv_pct(17));
    lv_obj_set_align(panel, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(panel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(panel, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_WASH, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(panel, SERVICE_GUI_THEME_BORDER, SERVICE_GUI_THEME_ACCENT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(panel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(panel, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(panel, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(panel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(panel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(panel, SERVICE_GUI_THEME_BG, SERVICE_GUI_THEME_WASH, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(panel, 80, LV_PART_MAIN | LV_STATE_PRESSED);
    service_gui_theme_style_add(panel, SERVICE_GUI_THEME_OUTLINE, SERVICE_GUI_THEME_INK, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_opa(panel, 80, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(panel, 1, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_pad(panel, 0, LV_PART_MAIN | LV_STATE_PRESSED);

    info = lv_obj_create(panel);
    if (info == NULL)
    {
        lv_obj_del(panel);
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
        lv_obj_del(panel);
        return SERVICE_ERROR;
    }

    lv_obj_set_width(name, lv_pct(100));
    lv_obj_set_height(name, LV_SIZE_CONTENT);
    lv_obj_set_align(name, LV_ALIGN_CENTER);
    lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_clear_flag(name, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    service_gui_theme_style_add(name, SERVICE_GUI_THEME_TEXT, SERVICE_GUI_THEME_ACCENT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(name, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_width(creator, lv_pct(100));
    lv_obj_set_height(creator, LV_SIZE_CONTENT);
    lv_obj_set_align(creator, LV_ALIGN_CENTER);
    lv_label_set_long_mode(creator, LV_LABEL_LONG_DOT);
    lv_obj_clear_flag(creator, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    service_gui_theme_style_add(creator, SERVICE_GUI_THEME_TEXT, SERVICE_GUI_THEME_INK, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(creator, 180, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(creator, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_width(status, LV_SIZE_CONTENT);
    lv_obj_set_height(status, LV_SIZE_CONTENT);
    lv_obj_set_align(status, LV_ALIGN_CENTER);
    lv_label_set_text(status, LV_SYMBOL_AUDIO);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    service_gui_theme_style_add(status, SERVICE_GUI_THEME_TEXT, SERVICE_GUI_THEME_ACCENT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(status, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        panel,
        service_gui_main_queue_on_panel_clicked,
        LV_EVENT_CLICKED,
        NULL);

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
        lv_obj_set_style_text_opa(
            status,
            255,
            LV_PART_MAIN | LV_STATE_DEFAULT);
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
        lv_obj_set_style_border_opa(
            panel,
            255,
            LV_PART_MAIN | LV_STATE_DEFAULT);
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
 * @brief 套用当前/非当前样式，不改曲名与歌手文字。
 * @param[in,out] row 已按范本构造的行。
 * @param[in] is_current 是否为窗口内正在播放的那一行。
 * @note 只改 Border Opa、符号 Opa、曲名色和 Long mode；不改 Border Width。
 *       曲名高度一律锁成一行：LVGL 8 的 DOT 看高度溢出；SCROLL_CIRCULAR 靠宽度
 *       上限横向滚，不需要 SIZE_CONTENT。点按切当前行时只走本函数，避免整窗改字。
 */
static void service_gui_main_queue_apply_row_style(
    Service_GUI_MainQueueRowTypeDef *row,
    bool is_current)
{
    const lv_font_t *name_font;

    service_gui_main_queue_set_current_border_visible(row->panel, is_current);
    service_gui_main_queue_set_status_visible(row->status, is_current);

    lv_obj_set_width(row->name, lv_pct(100));
    name_font = lv_obj_get_style_text_font(row->name, LV_PART_MAIN);
    lv_obj_set_height(row->name, lv_font_get_line_height(name_font));

    if (is_current)
    {
        service_gui_theme_style_replace(
            row->name,
            SERVICE_GUI_THEME_TEXT,
            SERVICE_GUI_THEME_ACCENT,
            LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_long_mode(row->name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
    else
    {
        service_gui_theme_style_replace(
            row->name,
            SERVICE_GUI_THEME_TEXT,
            SERVICE_GUI_THEME_INK,
            LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_long_mode(row->name, LV_LABEL_LONG_DOT);
    }
}

/**
 * @brief 填写一行文字并套用当前/非当前样式。
 * @param[in,out] row 已按范本构造的行。
 * @param[in] title 曲名显示文本。
 * @param[in] artist 歌手文本。
 * @param[in] is_current 是否为窗口内正在播放的那一行。
 */
static void service_gui_main_queue_apply_row(
    Service_GUI_MainQueueRowTypeDef *row,
    const char *title,
    const char *artist,
    bool is_current)
{
    service_gui_main_queue_apply_row_style(row, is_current);
    lv_label_set_text(row->name, title);
    lv_label_set_text(row->creator, artist);
}

/**
 * @brief 只刷新本窗各行的当前/非当前样式，不改文字、不转 head。
 * @param[in] current_index 新的播放列表下标。
 */
static void service_gui_main_queue_refresh_current(uint16_t current_index)
{
    uint16_t i;

    for (i = 0U; i < service_gui_main_queue_applied_length; i++)
    {
        service_gui_main_queue_apply_row_style(
            &service_gui_main_queue_rows[i],
            (current_index != SERVICE_GUI_QUEUE_NO_CURRENT) &&
                (((uint32_t)service_gui_main_queue_applied_index +
                  (uint32_t)i) == (uint32_t)current_index));
    }
}

/**
 * @brief 点按可见行时立刻改样式，并记下播放列表下标供 GUI Task 取走。
 * @param[in] e LVGL 点按事件。
 * @note 子对象已关掉 Clickable，命中落在 Panel。不打开文件、不解码。
 */
static void service_gui_main_queue_on_panel_clicked(lv_event_t *e)
{
    lv_obj_t *panel;
    uint16_t i;
    uint16_t sheet_index;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    panel = lv_event_get_current_target(e);
    if ((panel == NULL) || (service_gui_main_queue_applied_length == 0U))
    {
        return;
    }

    for (i = 0U; i < service_gui_main_queue_applied_length; i++)
    {
        if (service_gui_main_queue_rows[i].panel != panel)
        {
            continue;
        }

        sheet_index = (uint16_t)((uint32_t)service_gui_main_queue_applied_index +
                                 (uint32_t)i);
        service_gui_main_queue_refresh_current(sheet_index);
        service_gui_input_post(
            SERVICE_GUI_INPUT_MUSIC_QUEUE_SELECT,
            sheet_index);
        return;
    }
}

/**
 * @brief 显示或隐藏已构造的一行，不销毁对象。
 * @param[in,out] row 已按范本构造的行。
 * @param[in] hidden 为真则 Hidden，为假则参与 QueueTab Flex。
 */
static void service_gui_main_queue_set_row_hidden(
    Service_GUI_MainQueueRowTypeDef *row,
    bool hidden)
{
    if (row->panel == NULL)
    {
        return;
    }

    if (hidden)
    {
        lv_obj_add_flag(row->panel, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(row->panel, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 一行加 QueueTab 行距，作为整行滚动的步长。
 * @return 步长像素；尚无行或高度未布局时为 0。
 */
static lv_coord_t service_gui_main_queue_row_stride(void)
{
    lv_coord_t row_h;
    lv_coord_t pad_row;

    if ((service_gui_main_queue_created == 0U) ||
        (service_gui_main_queue_rows[0].panel == NULL))
    {
        return 0;
    }

    row_h = lv_obj_get_height(service_gui_main_queue_rows[0].panel);
    if (row_h <= 0)
    {
        return 0;
    }

    pad_row = lv_obj_get_style_pad_row(service_gui_main_queue_music->queue_tab, LV_PART_MAIN);
    return (lv_coord_t)(row_h + pad_row);
}

/**
 * @brief 读取 QueueTab 顶部已滚出的整行数。
 * @return 完整滚出顶部的行数；对象未就绪或尚无行时为 0。
 */
uint16_t service_gui_main_queue_scroll_lead(void)
{
    lv_coord_t stride;
    lv_coord_t y;
    uint16_t lead;

    if ((service_gui_main_queue_music == NULL) ||
        (service_gui_main_queue_music->queue_tab == NULL))
    {
        return 0U;
    }

    stride = service_gui_main_queue_row_stride();
    if (stride <= 0)
    {
        return 0U;
    }

    y = lv_obj_get_scroll_y(service_gui_main_queue_music->queue_tab);
    if (y < 0)
    {
        y = 0;
    }

    lead = (uint16_t)(y / stride);
    if ((service_gui_main_queue_created > 0U) &&
        (lead >= service_gui_main_queue_created))
    {
        lead = (uint16_t)(service_gui_main_queue_created - 1U);
    }

    return lead;
}

/**
 * @brief 把头行挪到队尾，Flex 顺序与数组一致。
 * @param[in] count 参与回收的已构造行数。
 */
static void service_gui_main_queue_rotate_down(uint16_t count)
{
    Service_GUI_MainQueueRowTypeDef head;
    uint16_t i;

    head = service_gui_main_queue_rows[0];
    for (i = 0U; i < (uint16_t)(count - 1U); i++)
    {
        service_gui_main_queue_rows[i] = service_gui_main_queue_rows[i + 1U];
    }

    service_gui_main_queue_rows[count - 1U] = head;
    lv_obj_move_to_index(
        head.panel,
        (int32_t)lv_obj_get_child_cnt(service_gui_main_queue_music->queue_tab) - 1);
}

/**
 * @brief 把尾行挪到原头行的位置，作为新的头行。
 * @param[in] count 参与回收的已构造行数。
 */
static void service_gui_main_queue_rotate_up(uint16_t count)
{
    Service_GUI_MainQueueRowTypeDef tail;
    int32_t head_index;
    uint16_t i;

    tail = service_gui_main_queue_rows[count - 1U];
    head_index = (int32_t)lv_obj_get_index(service_gui_main_queue_rows[0].panel);
    for (i = (uint16_t)(count - 1U); i > 0U; i--)
    {
        service_gui_main_queue_rows[i] = service_gui_main_queue_rows[i - 1U];
    }

    service_gui_main_queue_rows[0] = tail;
    lv_obj_move_to_index(tail.panel, head_index);
}

/**
 * @brief 按窗口起点差转 head；跨度达到已构造行数时不转，整窗改字。
 * @param[in] delta 新 Index 减旧 Index。
 * @param[in] count 已构造行数。
 */
static void service_gui_main_queue_rotate(int32_t delta, uint16_t count)
{
    if ((count < 2U) || (delta == 0) ||
        (delta >= (int32_t)count) || (delta <= -(int32_t)count))
    {
        return;
    }

    while (delta > 0)
    {
        service_gui_main_queue_rotate_down(count);
        delta--;
    }

    while (delta < 0)
    {
        service_gui_main_queue_rotate_up(count);
        delta++;
    }
}

/**
 * @brief 转掉几行就从当前 scroll_y 扣几行高度，保留手指剩下的像素，不吸回整页。
 * @param[in] index_delta 新窗口起点减旧起点。
 */
static void service_gui_main_queue_adjust_scroll(int32_t index_delta)
{
    lv_coord_t stride;
    lv_coord_t y;
    int32_t next_y;

    if (index_delta == 0)
    {
        return;
    }

    stride = service_gui_main_queue_row_stride();
    if (stride <= 0)
    {
        return;
    }

    y = lv_obj_get_scroll_y(service_gui_main_queue_music->queue_tab);
    next_y = (int32_t)y - (index_delta * (int32_t)stride);
    if (next_y < 0)
    {
        next_y = 0;
    }

    lv_obj_scroll_to_y(service_gui_main_queue_music->queue_tab, (lv_coord_t)next_y, LV_ANIM_OFF);
}

/**
 * @brief 打开 QueueTab 竖向滚动，不预先造行。
 * @retval SERVICE_OK 已就绪。
 * @retval SERVICE_NOT_READY QueueTab 尚未创建。
 * @note 必须在界面创建之后、Boot 占用 Canvas 之前由 Main 编排入口调用。
 *       可见行等 GUI Task 经 QueueApply 按 Length 填入。
 */
Service_StatusTypeDef service_gui_main_queue_prepare(void)
{
    service_gui_main_queue_music = &service_gui_view_get()->music;

    if (service_gui_main_queue_music->queue_tab == NULL)
    {
        return SERVICE_NOT_READY;
    }

    lv_obj_add_flag(service_gui_main_queue_music->queue_tab, LV_OBJ_FLAG_SCROLLABLE);
    return SERVICE_OK;
}

/**
 * @brief 按 Length 填 Queue 可见行；不够的 Hidden，不够用的按范本补造。
 * @param[in] titles 曲名字符串指针表，Length 为 0 时允许为 NULL。
 * @param[in] length 本窗实际条数，0..SERVICE_GUI_MAIN_QUEUE_MAX_ROWS。
 * @param[in] window_index 本窗在播放列表上的起点。
 * @param[in] current_index 正在播放的播放列表下标；无当前曲时为
 *            SERVICE_GUI_QUEUE_NO_CURRENT。
 * @retval SERVICE_OK 已按 Length 显示行，或 Length 为 0 已全部 Hidden。
 * @retval SERVICE_INVALID_PARAM Length 超上限，或 Length 非 0 但 titles 为空。
 * @retval SERVICE_NOT_READY QueueTab 或范本尚未导出。
 * @retval SERVICE_ERROR 补造行时 LVGL 未能创建对象。
 * @note 歌手空串。不包含 storage_listbuffer.h。已有行转 head，不无限 create。
 */
Service_StatusTypeDef service_gui_main_queue_apply(
    const char **titles,
    uint16_t length,
    uint16_t window_index,
    uint16_t current_index)
{
    Service_StatusTypeDef status;
    uint16_t i;
    uint16_t old_index;
    int32_t delta;

    if ((service_gui_main_queue_music == NULL) ||
        (service_gui_main_queue_music->queue_tab == NULL))
    {
        return SERVICE_NOT_READY;
    }

    if ((length > SERVICE_GUI_MAIN_QUEUE_MAX_ROWS) ||
        ((length > 0U) && (titles == NULL)))
    {
        return SERVICE_INVALID_PARAM;
    }

    old_index = service_gui_main_queue_applied_index;

    if (length == 0U)
    {
        for (i = 0U; i < service_gui_main_queue_created; i++)
        {
            service_gui_main_queue_set_row_hidden(
                &service_gui_main_queue_rows[i],
                true);
        }

        service_gui_main_queue_applied_index = 0U;
        service_gui_main_queue_applied_length = 0U;
        service_gui_input_drop_queue_select();
        lv_obj_scroll_to_y(service_gui_main_queue_music->queue_tab, 0, LV_ANIM_OFF);
        (void)current_index;
        return SERVICE_OK;
    }

    for (i = 0U; i < length; i++)
    {
        if (i >= service_gui_main_queue_created)
        {
            status = service_gui_main_queue_create_row(
                &service_gui_main_queue_rows[i]);
            if (status != SERVICE_OK)
            {
                return status;
            }

            service_gui_main_queue_created = (uint16_t)(i + 1U);
        }
    }

    delta = (int32_t)window_index - (int32_t)old_index;
    service_gui_main_queue_rotate(delta, service_gui_main_queue_created);

    for (i = 0U; i < length; i++)
    {
        service_gui_main_queue_set_row_hidden(
            &service_gui_main_queue_rows[i],
            false);
        service_gui_main_queue_apply_row(
            &service_gui_main_queue_rows[i],
            (titles[i] != NULL) ? titles[i] : "",
            "",
            (current_index != SERVICE_GUI_QUEUE_NO_CURRENT) &&
                (((uint32_t)window_index + (uint32_t)i) ==
                 (uint32_t)current_index));
    }

    for (i = length; i < service_gui_main_queue_created; i++)
    {
        service_gui_main_queue_set_row_hidden(
            &service_gui_main_queue_rows[i],
            true);
    }

    service_gui_main_queue_adjust_scroll(delta);
    service_gui_main_queue_applied_index = window_index;
    service_gui_main_queue_applied_length = length;
    return SERVICE_OK;
}
