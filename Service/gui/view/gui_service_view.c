/**
  ******************************************************************************
  * @file    gui_service_view.c
  * @brief   GUI Service 私有界面层实现：按顺序创建全部 Screen 并加载 Boot。
  ******************************************************************************
  */

#include "Service/gui/view/gui_service_view.h"
#include "Service/gui/view/gui_service_view_screens.h"

extern const lv_img_dsc_t ui_img_wallpaper_indigo_mist_soft_dark_png;

static Service_GUI_ViewTypeDef service_gui_view;

/**
 * @brief Boot Screen 被 LVGL 删除时清空其句柄。
 * @param[in] event Boot Screen 的 LV_EVENT_DELETE。
 * @note Boot 切到 BootReveal 后即被删除；之后访问 Boot 的行为 Module 必须按 NULL 处理。
 */
static void service_gui_view_on_boot_deleted(lv_event_t *event)
{
    (void)event;

    service_gui_view.boot.screen = NULL;
    service_gui_view.boot.orbit_ring = NULL;
}

/**
 * @brief 创建全部 Screen、加载 Boot，并填写对象句柄。
 * @retval SERVICE_OK 全部对象已创建。
 * @retval SERVICE_ERROR 任一 Screen 创建失败。
 * @note 只能由 GUI Task 在 LVGL 显示驱动注册后调用一次。先装 basic theme，
 *       之后创建的对象都以它为默认样式；Main 先于 Lock 创建，因为 Lock 解锁以 Main 为目标。
 */
Service_StatusTypeDef service_gui_view_create(void)
{
    lv_disp_t *disp = lv_disp_get_default();
    const lv_img_dsc_t *wallpaper = service_gui_view_wallpaper();

    lv_disp_set_theme(disp, lv_theme_basic_init(disp));

    service_gui_view_boot_create(&service_gui_view, wallpaper);
    service_gui_view_main_create(&service_gui_view, wallpaper);
    service_gui_view_lock_create(&service_gui_view, wallpaper);

    if ((service_gui_view.boot.screen == NULL) || (service_gui_view.boot_reveal == NULL) ||
        (service_gui_view.lock == NULL) || (service_gui_view.main.screen == NULL))
    {
        return SERVICE_ERROR;
    }

    lv_obj_add_event_cb(service_gui_view.boot.screen, service_gui_view_on_boot_deleted,
                        LV_EVENT_DELETE, NULL);
    lv_disp_load_scr(service_gui_view.boot.screen);

    return SERVICE_OK;
}

/**
 * @brief 读取对象句柄。
 * @return 句柄集合；service_gui_view_create() 之前各字段为 NULL。
 */
const Service_GUI_ViewTypeDef *service_gui_view_get(void)
{
    return &service_gui_view;
}

/**
 * @brief 当前系统壁纸（清晰帧）。
 */
const lv_img_dsc_t *service_gui_view_wallpaper(void)
{
    return &ui_img_wallpaper_indigo_mist_soft_dark_png;
}
