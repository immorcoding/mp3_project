/**
  ******************************************************************************
  * @file    gui_service_main_pager.c
  * @brief   Main Screen 三页循环分页与底部分页指示器实现。
  *
  * @details
  *          本 Module 只管理 MainPageContainer 的物理槽位、松手吸附、首尾循环
  *          重排以及逻辑页面对应的底部圆点动画。它不创建或销毁 SquareLine 对象，
  *          不读取壁纸像素、不持有 Canvas 缓冲，也不覆盖 SquareLine 定义的颜色、
  *          圆角、布局或其他静态视觉样式。
  ******************************************************************************
  */

#include "Service/gui/main/pager/gui_service_main_pager.h"
#include "Service/gui/main/pager/gui_service_main_pager_config.h"

#include <stdbool.h>
#include <stdint.h>

#include "GUI/ui.h"

/**
 * @brief MainPageContainer 内三张 Page 的物理槽位编号。
 * @note LEFT、CENTER、RIGHT 分别位于一个 Viewport 的 0%、100%、200% 水平偏移处。
 *       循环切页时轮换既有对象在这三个位置的映射，不复制或销毁任何 Page。
 */
typedef enum
{
    SERVICE_GUI_MAIN_PAGER_SLOT_LEFT = 0,
    SERVICE_GUI_MAIN_PAGER_SLOT_CENTER,
    SERVICE_GUI_MAIN_PAGER_SLOT_RIGHT,
    SERVICE_GUI_MAIN_PAGER_SLOT_COUNT,
} Service_GUI_MainPagerSlotTypeDef;

/** @brief 三张逻辑 Page 的固定身份编号，仅用于底部圆点映射。 */
typedef enum
{
    SERVICE_GUI_MAIN_PAGER_PAGE_SETTINGS = 0,
    SERVICE_GUI_MAIN_PAGER_PAGE_MUSIC,
    SERVICE_GUI_MAIN_PAGER_PAGE_BOOKS,
    SERVICE_GUI_MAIN_PAGER_PAGE_COUNT,
} Service_GUI_MainPagerPageTypeDef;

/** @brief 当前已完成吸附的物理槽位。 */
static Service_GUI_MainPagerSlotTypeDef service_gui_main_pager_slot =
    SERVICE_GUI_MAIN_PAGER_SLOT_CENTER;

/** @brief 当前程序化吸附结束后将生效的物理槽位。 */
static Service_GUI_MainPagerSlotTypeDef service_gui_main_pager_pending_slot =
    SERVICE_GUI_MAIN_PAGER_SLOT_CENTER;

/** @brief 当前 LV_EVENT_SCROLL_END 是否来自本 Module 发起的吸附动画。 */
static bool service_gui_main_pager_snap_in_progress;

/** @brief 当前 LV_EVENT_SCROLL_END 是否来自循环重排后的无动画回中。 */
static bool service_gui_main_pager_recenter_in_progress;

/**
 * @brief 当前物理槽位到 SquareLine Page 对象的映射。
 * @note 下标只表达物理位置；对象映射会在循环切页后轮换，故逻辑 Page 不依赖
 *       固定像素坐标或固定槽位。
 */
static lv_obj_t *service_gui_main_pager_slots[
    SERVICE_GUI_MAIN_PAGER_SLOT_COUNT];

/** @brief 逻辑 Page 到 SquareLine 导出分页指示器对象的固定映射。 */
static lv_obj_t *service_gui_main_pager_dots[
    SERVICE_GUI_MAIN_PAGER_PAGE_COUNT];

/** @brief 当前逻辑活动页，用于确认应收缩的旧分页指示器。 */
static lv_obj_t *service_gui_main_pager_active_page;

/** @brief SquareLine 导出的非活动圆点宽度与背景透明度基线。 */
static lv_coord_t service_gui_main_pager_inactive_dot_width;
static lv_opa_t service_gui_main_pager_inactive_dot_opa;

/** @brief SquareLine 导出的活动胶囊宽度与背景透明度基线。 */
static lv_coord_t service_gui_main_pager_active_dot_width;
static lv_opa_t service_gui_main_pager_active_dot_opa;

/** @brief 已绑定分页回调的 SquareLine MainPageContainer。 */
static lv_obj_t *service_gui_main_pager_container;

/**
 * @brief 绑定 SquareLine 导出的三张 Main Page 到初始物理槽位。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY MainPageContainer 或任一 Page 尚未创建。
 * @note 此函数只在 Main 重建后恢复 SquareLine 导出的初始顺序。正常循环期间由
 *       service_gui_main_pager_recenter_loop() 维护该映射，不能在滚动回调中重复调用。
 */
static Service_StatusTypeDef service_gui_main_pager_bind_slots(void)
{
    if ((ui_MainPageContainer == NULL) ||
        (ui_SettingsPageContainer == NULL) ||
        (ui_MusicPageContainer == NULL) ||
        (ui_BooksPageContainer == NULL))
    {
        return SERVICE_NOT_READY;
    }

    service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_LEFT] =
        ui_SettingsPageContainer;
    service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_CENTER] =
        ui_MusicPageContainer;
    service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_RIGHT] =
        ui_BooksPageContainer;

    return SERVICE_OK;
}

/**
 * @brief 绑定 SquareLine 导出的逻辑 Page 与底部分页指示器。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY 任一指示器未创建，或其初始视觉尺寸无效。
 * @note 本函数从 SquareLine 导出的 Settings、Music 指示器读取非活动与活动视觉
 *       基线。后续动画只使用这些运行时读取值，避免将 UI 色彩或尺寸复制进 Service。
 */
static Service_StatusTypeDef service_gui_main_pager_bind_dots(void)
{
    if ((ui_DotSettings == NULL) ||
        (ui_DotMusic == NULL) ||
        (ui_DotBooks == NULL))
    {
        return SERVICE_NOT_READY;
    }

    service_gui_main_pager_dots[SERVICE_GUI_MAIN_PAGER_PAGE_SETTINGS] =
        ui_DotSettings;
    service_gui_main_pager_dots[SERVICE_GUI_MAIN_PAGER_PAGE_MUSIC] =
        ui_DotMusic;
    service_gui_main_pager_dots[SERVICE_GUI_MAIN_PAGER_PAGE_BOOKS] =
        ui_DotBooks;
    service_gui_main_pager_inactive_dot_width = lv_obj_get_width(ui_DotSettings);
    service_gui_main_pager_inactive_dot_opa = lv_obj_get_style_bg_opa(
        ui_DotSettings,
        LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_main_pager_active_dot_width = lv_obj_get_width(ui_DotMusic);
    service_gui_main_pager_active_dot_opa = lv_obj_get_style_bg_opa(
        ui_DotMusic,
        LV_PART_MAIN | LV_STATE_DEFAULT);

    if ((service_gui_main_pager_inactive_dot_width <= 0) ||
        (service_gui_main_pager_active_dot_width <= 0))
    {
        return SERVICE_NOT_READY;
    }

    service_gui_main_pager_active_page = ui_MusicPageContainer;

    return SERVICE_OK;
}

/**
 * @brief 获取指定逻辑 Page 对应的 SquareLine 分页指示器。
 * @param page MainPageContainer 内的一个逻辑 Page 对象。
 * @return 对应指示器；若 Page 不属于当前 Main 分页结构则返回 NULL。
 */
static lv_obj_t *service_gui_main_pager_get_dot(const lv_obj_t *page)
{
    if (page == ui_SettingsPageContainer)
    {
        return service_gui_main_pager_dots[
            SERVICE_GUI_MAIN_PAGER_PAGE_SETTINGS];
    }

    if (page == ui_MusicPageContainer)
    {
        return service_gui_main_pager_dots[
            SERVICE_GUI_MAIN_PAGER_PAGE_MUSIC];
    }

    if (page == ui_BooksPageContainer)
    {
        return service_gui_main_pager_dots[
            SERVICE_GUI_MAIN_PAGER_PAGE_BOOKS];
    }

    return NULL;
}

/**
 * @brief 供 LVGL 动画设置分页指示器宽度。
 * @param object 分页指示器对象。
 * @param width 新宽度。
 */
static void service_gui_main_pager_set_dot_width(void *object, int32_t width)
{
    lv_obj_set_width((lv_obj_t *)object, (lv_coord_t)width);
}

/**
 * @brief 供 LVGL 动画设置分页指示器背景透明度。
 * @param object 分页指示器对象。
 * @param opa 新背景透明度。
 */
static void service_gui_main_pager_set_dot_opa(void *object, int32_t opa)
{
    lv_obj_set_style_bg_opa(
        (lv_obj_t *)object,
        (lv_opa_t)opa,
        LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * @brief 将一个分页指示器的当前宽度和透明度动画过渡到指定状态。
 * @param dot 指示器对象。
 * @param target_width 目标宽度。
 * @param target_opa 目标背景透明度。
 * @note 每次启动前删除同一对象上的同类动画，快速连续切页时从当前已绘制状态平滑
 *       过渡，不会保留过期的伸缩或透明度动画。
 */
static void service_gui_main_pager_animate_dot(
    lv_obj_t *dot,
    lv_coord_t target_width,
    lv_opa_t target_opa)
{
    lv_anim_t animation;

    if (dot == NULL)
    {
        return;
    }

    lv_anim_del(dot, service_gui_main_pager_set_dot_width);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, dot);
    lv_anim_set_exec_cb(&animation, service_gui_main_pager_set_dot_width);
    lv_anim_set_values(
        &animation,
        lv_obj_get_width(dot),
        target_width);
    lv_anim_set_time(
        &animation,
        SERVICE_GUI_MAIN_PAGER_DOT_ANIMATION_TIME_MS);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);

    lv_anim_del(dot, service_gui_main_pager_set_dot_opa);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, dot);
    lv_anim_set_exec_cb(&animation, service_gui_main_pager_set_dot_opa);
    lv_anim_set_values(
        &animation,
        lv_obj_get_style_bg_opa(dot, LV_PART_MAIN | LV_STATE_DEFAULT),
        target_opa);
    lv_anim_set_time(
        &animation,
        SERVICE_GUI_MAIN_PAGER_DOT_ANIMATION_TIME_MS);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

/**
 * @brief 切换当前逻辑 Page 对应的分页指示器视觉状态。
 * @param next_page 已判定为下一页的逻辑 Page 对象。
 * @note 旧页胶囊收缩为 SquareLine 定义的圆点，新页圆点伸展为 SquareLine 定义的
 *       胶囊。没有逻辑页变化时不启动动画。
 */
static void service_gui_main_pager_update_dot(lv_obj_t *next_page)
{
    lv_obj_t *previous_dot;
    lv_obj_t *next_dot;

    if ((next_page == NULL) ||
        (next_page == service_gui_main_pager_active_page))
    {
        return;
    }

    previous_dot = service_gui_main_pager_get_dot(
        service_gui_main_pager_active_page);
    next_dot = service_gui_main_pager_get_dot(next_page);

    if ((previous_dot == NULL) || (next_dot == NULL))
    {
        return;
    }

    service_gui_main_pager_animate_dot(
        previous_dot,
        service_gui_main_pager_inactive_dot_width,
        service_gui_main_pager_inactive_dot_opa);
    service_gui_main_pager_animate_dot(
        next_dot,
        service_gui_main_pager_active_dot_width,
        service_gui_main_pager_active_dot_opa);
    service_gui_main_pager_active_page = next_page;
}

/**
 * @brief 将当前 Page 槽位映射写回 SquareLine 分页视口的位置。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY MainPageContainer 或槽位对象尚未就绪。
 * @note 位置使用与 SquareLine 初始导出一致的百分比语义，避免将显示分辨率或
 *       Viewport 像素宽度固定写入 Page 重排逻辑。
 */
static Service_StatusTypeDef service_gui_main_pager_apply_slots(void)
{
    if ((ui_MainPageContainer == NULL) ||
        (service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_LEFT] == NULL) ||
        (service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_CENTER] == NULL) ||
        (service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_RIGHT] == NULL))
    {
        return SERVICE_NOT_READY;
    }

    lv_obj_set_x(
        service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_LEFT],
        lv_pct(0));
    lv_obj_set_x(
        service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_CENTER],
        lv_pct(100));
    lv_obj_set_x(
        service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_RIGHT],
        lv_pct(200));

    return SERVICE_OK;
}

/**
 * @brief 在达到两端物理槽位后循环轮换 Page，并无动画回到中间槽位。
 * @retval SERVICE_OK 成功，或当前已位于中间槽位而无需重排。
 * @retval SERVICE_NOT_READY Page、Viewport 或布局宽度尚未就绪。
 * @note 右端活动页触发左循环，左端活动页触发右循环。位置重写与无动画回中发生在
 *       同一次 LVGL 事件处理中，下一帧只会显示重排后的稳定结果。
 */
static Service_StatusTypeDef service_gui_main_pager_recenter_loop(void)
{
    Service_StatusTypeDef status;
    lv_obj_t *temporary_page;
    lv_coord_t viewport_width;

    if (service_gui_main_pager_slot == SERVICE_GUI_MAIN_PAGER_SLOT_CENTER)
    {
        return SERVICE_OK;
    }

    if (service_gui_main_pager_slot == SERVICE_GUI_MAIN_PAGER_SLOT_RIGHT)
    {
        temporary_page =
            service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_LEFT];
        service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_LEFT] =
            service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_CENTER];
        service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_CENTER] =
            service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_RIGHT];
        service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_RIGHT] =
            temporary_page;
    }
    else if (service_gui_main_pager_slot == SERVICE_GUI_MAIN_PAGER_SLOT_LEFT)
    {
        temporary_page =
            service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_RIGHT];
        service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_RIGHT] =
            service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_CENTER];
        service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_CENTER] =
            service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_LEFT];
        service_gui_main_pager_slots[SERVICE_GUI_MAIN_PAGER_SLOT_LEFT] =
            temporary_page;
    }
    else
    {
        return SERVICE_NOT_READY;
    }

    status = service_gui_main_pager_apply_slots();

    if (status != SERVICE_OK)
    {
        return status;
    }

    /* 确保新百分比槽位已解析，再按一个 Viewport 宽度回到中间。 */
    lv_obj_update_layout(ui_Main);
    viewport_width = lv_obj_get_width(ui_MainPageContainer);

    if (viewport_width <= 0)
    {
        return SERVICE_NOT_READY;
    }

    service_gui_main_pager_slot = SERVICE_GUI_MAIN_PAGER_SLOT_CENTER;
    service_gui_main_pager_pending_slot = SERVICE_GUI_MAIN_PAGER_SLOT_CENTER;
    service_gui_main_pager_recenter_in_progress = true;
    lv_obj_scroll_to_x(
        ui_MainPageContainer,
        viewport_width,
        LV_ANIM_OFF);

    return SERVICE_OK;
}

/**
 * @brief 在用户结束横滑后吸附至当前或相邻的固定物理槽位。
 * @param event MainPageContainer 的 LV_EVENT_SCROLL_END 事件。
 * @note 判定基于当前已完成槽位而非简单取最近页，故一次很长的拖动最多只跨越一页。
 *       本 Module 发起的滚动动画结束也会产生同一事件；由私有状态识别后只提交目标
 *       槽位，避免把程序化吸附递归判定为新的用户手势。
 */
static void service_gui_main_pager_scroll_end_event(lv_event_t *event)
{
    lv_coord_t viewport_width;
    lv_coord_t current_scroll_x;
    lv_coord_t current_slot_scroll_x;
    lv_coord_t switch_threshold;
    lv_coord_t target_scroll_x;
    Service_GUI_MainPagerSlotTypeDef target_slot;

    if ((event == NULL) ||
        (lv_event_get_code(event) != LV_EVENT_SCROLL_END) ||
        (lv_event_get_target(event) != ui_MainPageContainer))
    {
        return;
    }

    if (service_gui_main_pager_recenter_in_progress)
    {
        service_gui_main_pager_recenter_in_progress = false;
        return;
    }

    if (service_gui_main_pager_snap_in_progress)
    {
        service_gui_main_pager_slot = service_gui_main_pager_pending_slot;
        service_gui_main_pager_snap_in_progress = false;
        (void)service_gui_main_pager_recenter_loop();
        return;
    }

    viewport_width = lv_obj_get_width(ui_MainPageContainer);

    if (viewport_width <= 0)
    {
        return;
    }

    current_scroll_x = lv_obj_get_scroll_x(ui_MainPageContainer);
    current_slot_scroll_x =
        (lv_coord_t)(service_gui_main_pager_slot * viewport_width);
    switch_threshold =
        (lv_coord_t)((viewport_width *
                      (lv_coord_t)SERVICE_GUI_MAIN_PAGER_SWITCH_THRESHOLD_PERCENT) /
                     100);
    target_slot = service_gui_main_pager_slot;

    if ((current_scroll_x - current_slot_scroll_x >= switch_threshold) &&
        (service_gui_main_pager_slot < SERVICE_GUI_MAIN_PAGER_SLOT_RIGHT))
    {
        target_slot = (Service_GUI_MainPagerSlotTypeDef)(
            service_gui_main_pager_slot + 1);
    }
    else if ((current_slot_scroll_x - current_scroll_x >= switch_threshold) &&
             (service_gui_main_pager_slot > SERVICE_GUI_MAIN_PAGER_SLOT_LEFT))
    {
        target_slot = (Service_GUI_MainPagerSlotTypeDef)(
            service_gui_main_pager_slot - 1);
    }

    target_scroll_x = (lv_coord_t)(target_slot * viewport_width);

    if (current_scroll_x == target_scroll_x)
    {
        service_gui_main_pager_update_dot(
            service_gui_main_pager_slots[target_slot]);
        service_gui_main_pager_slot = target_slot;
        (void)service_gui_main_pager_recenter_loop();
        return;
    }

    service_gui_main_pager_pending_slot = target_slot;
    service_gui_main_pager_snap_in_progress = true;
    service_gui_main_pager_update_dot(
        service_gui_main_pager_slots[target_slot]);
    lv_obj_scroll_to_x(
        ui_MainPageContainer,
        target_scroll_x,
        LV_ANIM_ON);
}

/**
 * @brief 为 MainPageContainer 安装循环分页回调。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY MainPageContainer 尚未创建。
 * @note 同一导出 Viewport 只注册一次。Background Module 也会向同一对象注册独立的
 *       LV_EVENT_SCROLL 回调；两者只共享 LVGL 事件源，不共享状态。
 */
static Service_StatusTypeDef service_gui_main_pager_bind_scroll_end_event(void)
{
    if (ui_MainPageContainer == NULL)
    {
        return SERVICE_NOT_READY;
    }

    if (ui_MainPageContainer != service_gui_main_pager_container)
    {
        lv_obj_add_event_cb(
            ui_MainPageContainer,
            service_gui_main_pager_scroll_end_event,
            LV_EVENT_SCROLL_END,
            NULL);
        service_gui_main_pager_container = ui_MainPageContainer;
    }

    return SERVICE_OK;
}

/**
 * @brief 无动画定位 MainPageContainer 到中间的 MusicPage 槽位。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY MainPageContainer 尚未创建或布局宽度无效。
 * @note SquareLine 中初始 Settings、Music、Books 三页分别位于 LEFT、CENTER、RIGHT
 *       槽位。以一个 Viewport 宽度作为水平滚动位置即可显示中间的 Music Page。
 */
static Service_StatusTypeDef service_gui_main_pager_center_music_page(void)
{
    lv_coord_t viewport_width;

    if (ui_MainPageContainer == NULL)
    {
        return SERVICE_NOT_READY;
    }

    viewport_width = lv_obj_get_width(ui_MainPageContainer);

    if (viewport_width <= 0)
    {
        return SERVICE_NOT_READY;
    }

    lv_obj_scroll_to_x(
        ui_MainPageContainer,
        viewport_width,
        LV_ANIM_OFF);
    service_gui_main_pager_slot = SERVICE_GUI_MAIN_PAGER_SLOT_CENTER;
    service_gui_main_pager_pending_slot = SERVICE_GUI_MAIN_PAGER_SLOT_CENTER;
    service_gui_main_pager_snap_in_progress = false;
    service_gui_main_pager_recenter_in_progress = false;

    return SERVICE_OK;
}

/**
 * @brief 初始化 Main Screen 的循环分页和底部分页指示器。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY SquareLine Main 对象、布局或指示器尚未就绪。
 * @note 本函数在 ui_init() 后执行。它只使用 SquareLine 公开对象；静态视觉样式仍由
 *       SquareLine 定义。Background Module 必须在本函数成功完成后初始化，以便首次
 *       毛玻璃裁剪基于已经居中的 MusicPage 坐标。
 */
Service_StatusTypeDef service_gui_main_pager_prepare(void)
{
    Service_StatusTypeDef status;

    if ((ui_Main == NULL) ||
        (ui_MainPageContainer == NULL))
    {
        return SERVICE_NOT_READY;
    }

    /* 先触发布局，确保百分比尺寸和 Flex 布局均已解析为实际坐标。 */
    lv_obj_update_layout(ui_Main);

    status = service_gui_main_pager_bind_slots();

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = service_gui_main_pager_bind_dots();

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = service_gui_main_pager_center_music_page();

    if (status != SERVICE_OK)
    {
        return status;
    }

    return service_gui_main_pager_bind_scroll_end_event();
}
