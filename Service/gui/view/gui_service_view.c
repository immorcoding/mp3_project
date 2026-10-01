/**
  ******************************************************************************
  * @file    gui_service_view.c
  * @brief   GUI Service 私有界面层实现。
  *
  * @details
  *          当前对象仍由 SquareLine 导出的 ui_init() 创建，本 Module 只把导出对象
  *          收拢为句柄，使行为 Module 不再直接包含 GUI/ui.h。
  ******************************************************************************
  */

#include "Service/gui/view/gui_service_view.h"

#include "GUI/ui.h"

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
 * @retval SERVICE_ERROR 任一关键对象创建失败。
 * @note 只能由 GUI Task 在 LVGL 显示驱动注册后调用一次。
 */
Service_StatusTypeDef service_gui_view_create(void)
{
    ui_init();

    service_gui_view.boot.screen = ui_Boot;
    service_gui_view.boot.orbit_ring = ui_BootOrbitRing;
    service_gui_view.boot_reveal = ui_BootReveal;
    service_gui_view.lock = ui_Lock;

    service_gui_view.main.screen = ui_Main;
    service_gui_view.main.page_container = ui_MainPageContainer;
    service_gui_view.main.settings_page = ui_SettingsPageContainer;
    service_gui_view.main.music_page = ui_MusicPageContainer;
    service_gui_view.main.books_page = ui_BooksPageContainer;
    service_gui_view.main.dot_settings = ui_DotSettings;
    service_gui_view.main.dot_music = ui_DotMusic;
    service_gui_view.main.dot_books = ui_DotBooks;

    service_gui_view.music.tabs = ui_MusicModeTabs;
    service_gui_view.music.queue_tab = ui_QueueTab;
    service_gui_view.music.vinyl_image = ui_MusicPlayerVinylImage;
    service_gui_view.music.slider = ui_MusicPlayingSlider;
    service_gui_view.music.previous_button = ui_MusicPreviousButton;
    service_gui_view.music.play_pause_button = ui_MusicPlayPauseButton;
    service_gui_view.music.play_pause_icon = ui_MusicPlayPauseIcon;
    service_gui_view.music.next_button = ui_MusicNextButton;

    if ((ui_Boot == NULL) || (ui_BootReveal == NULL) ||
        (ui_Lock == NULL) || (ui_Main == NULL))
    {
        return SERVICE_ERROR;
    }

    lv_obj_add_event_cb(ui_Boot, service_gui_view_on_boot_deleted, LV_EVENT_DELETE, NULL);

    /* Queue 行由 queue/ 运行时构造，导出的单行范本不参与显示。 */
    if (ui_SongPanel1 != NULL)
    {
        lv_obj_add_flag(ui_SongPanel1, LV_OBJ_FLAG_HIDDEN);
    }

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
