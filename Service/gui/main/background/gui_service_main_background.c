/**
  ******************************************************************************
  * @file    gui_service_main_background.c
  * @brief   Main Screen 的 MusicModeTabs 局部毛玻璃与 Solid 薄层。
  *
  * @details
  *          本 Module 不修改 view/ 创建的静态视觉。Default 外观下，它在 Pager 完成
  *          布局和初始回中后读取对象实际屏幕坐标，以 Main Screen 为原点换算为壁纸
  *          坐标；随后从长期持有的全屏模糊壁纸裁剪出与 MusicModeTabs 等大的局部
  *          背景，并只把该运行时 Background image 绑定到 MusicModeTabs。
  *          MainPageContainer 滚动期间按控件当帧坐标重裁剪，确保玻璃区域始终采样
  *          其下方壁纸，且绝不在滚动回调中重复执行全屏软件模糊。Solid 外观改用
  *          半透明 Wash 薄层。
  ******************************************************************************
  */

#include "Service/gui/main/background/gui_service_main_background.h"
#include "Service/gui/main/background/gui_service_main_background_config.h"

#include <stdbool.h>
#include <stdint.h>

#include "Platform/lcd/platform_lcd.h"
#include "Service/gui/canvas/gui_service_canvas.h"
#include "Service/gui/canvas/gui_service_canvas_compositor.h"
#include "Service/gui/theme/gui_service_theme.h"
#include "Service/gui/theme/gui_service_theme_config.h"

#include "Service/gui/view/gui_service_view.h"

/**
 * @brief Main Screen 长期持有的 MusicModeTabs 局部毛玻璃裁剪像素缓冲。
 * @note 当前壁纸为 LV_IMG_CF_TRUE_COLOR_ALPHA，因此按同格式分配。它不由
 *       Canvas 工作区持有，后者可在本次裁剪结束后继续服务其他离屏效果。
 */
static uint8_t service_gui_main_background_tabs_buffer[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(
        SERVICE_GUI_MAIN_BACKGROUND_TABS_MAX_WIDTH,
        SERVICE_GUI_MAIN_BACKGROUND_TABS_MAX_HEIGHT)]
    __attribute__((section(".sdram_framebuffer"),
                   aligned(PLATFORM_DCACHE_LINE_SIZE)));

/** @brief 界面对象句柄，prepare 时取得。 */
static const Service_GUI_ViewTypeDef *service_gui_main_background_view;

/** @brief 清晰系统壁纸，首次需要毛玻璃时作为模糊源。 */
static const lv_img_dsc_t *service_gui_main_background_clear_wallpaper;

/** @brief 绑定 MusicModeTabs 局部背景的长期 LVGL 图片描述符。 */
static lv_img_dsc_t service_gui_main_background_tabs_image;

/**
 * @brief Main Screen 长期持有的全屏模糊壁纸像素缓冲。
 * @note Canvas 工作区会被 Boot 等其他离屏视觉效果复用，不能作为滚动时裁剪的
 *       数据源。本缓冲只在壁纸切换时重建，滚动回调仅从中复制局部像素。
 */
static uint8_t service_gui_main_background_blurred_wallpaper_buffer[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(
        PLATFORM_LCD_WIDTH,
        PLATFORM_LCD_HEIGHT)]
    __attribute__((section(".sdram_framebuffer"),
                   aligned(PLATFORM_DCACHE_LINE_SIZE)));

/** @brief 绑定 Main Screen 长期模糊壁纸缓冲的 LVGL 图片描述符。 */
static lv_img_dsc_t service_gui_main_background_blurred_wallpaper;

/** @brief 当前长期模糊壁纸是否已成功构建。 */
static bool service_gui_main_background_blurred_wallpaper_ready;

/** @brief 已绑定局部背景同步回调的 MainPageContainer。 */
static lv_obj_t *service_gui_main_background_page_container;

/**
 * @brief 按 Main Screen 原点将对象坐标转换为壁纸坐标区域。
 * @param object view/ 创建的目标对象。
 * @param main_area Main Screen 的实际屏幕区域。
 * @param image_area 返回的壁纸内闭区间裁剪区域。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY 对象或输出区域为空。
 * @note 坐标来自布局后的对象实际区域，故 view/ 中调整相对位置、百分比尺寸
 *       或 MAIN Radius 后，无需同步维护任何 Service 内像素常量。横滑时返回区域
 *       可以部分越出壁纸；调用方使用带透明越界填充的裁剪 Interface。输出尺寸仍
 *       必须不超过 gui_service_main_background_config.h 中的局部背景容量上限。
 */
static Service_StatusTypeDef service_gui_main_background_get_object_image_area(
    const lv_obj_t *object,
    const lv_area_t *main_area,
    lv_area_t *image_area)
{
    lv_area_t object_area;

    if ((object == NULL) || (main_area == NULL) || (image_area == NULL))
    {
        return SERVICE_NOT_READY;
    }

    lv_obj_get_coords(object, &object_area);

    image_area->x1 = object_area.x1 - main_area->x1;
    image_area->y1 = object_area.y1 - main_area->y1;
    image_area->x2 = object_area.x2 - main_area->x1;
    image_area->y2 = object_area.y2 - main_area->y1;

    return SERVICE_OK;
}

/**
 * @brief 按 MusicModeTabs 当前屏幕坐标更新其局部毛玻璃背景。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY Main、目标控件或长期模糊壁纸尚未就绪。
 * @retval SERVICE_INVALID_PARAM 当前对象尺寸超过预留裁剪缓冲，或图片描述符无效。
 * @note 该函数只从长期模糊壁纸复制局部像素，不做软件模糊。裁剪允许越出壁纸
 *       边界，越界部分透明，以支持 MainPageContainer 横滑过程中的半离屏状态。
 */
static Service_StatusTypeDef service_gui_main_background_refresh_tabs(void)
{
    Service_StatusTypeDef status;
    lv_area_t main_area;
    lv_area_t tabs_image_area;

    if (!service_gui_main_background_blurred_wallpaper_ready ||
        (service_gui_main_background_view->main.screen == NULL) ||
        (service_gui_main_background_view->music.tabs == NULL))
    {
        return SERVICE_NOT_READY;
    }

    lv_obj_get_coords(service_gui_main_background_view->main.screen, &main_area);

    status = service_gui_main_background_get_object_image_area(
        service_gui_main_background_view->music.tabs,
        &main_area,
        &tabs_image_area);

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = service_gui_canvas_extract_image_region_padded(
        &service_gui_main_background_blurred_wallpaper,
        &tabs_image_area,
        service_gui_main_background_tabs_buffer,
        sizeof(service_gui_main_background_tabs_buffer),
        &service_gui_main_background_tabs_image);

    if (status != SERVICE_OK)
    {
        return status;
    }

    lv_img_cache_invalidate_src(&service_gui_main_background_tabs_image);
    lv_obj_invalidate(service_gui_main_background_view->music.tabs);

    return SERVICE_OK;
}

/**
 * @brief 在 MainPageContainer 横滑时重新裁剪 MusicModeTabs 下方的模糊壁纸。
 * @param event LVGL 发送的滚动事件。
 * @note 回调直接绑定到 MainPageContainer，它本身就是实际横向滚动的 Viewport。
 *       Solid 外观不显示毛玻璃，跳过裁剪。
 */
static void service_gui_main_background_page_scroll_event(lv_event_t *event)
{
    if ((event == NULL) || (lv_event_get_code(event) != LV_EVENT_SCROLL) ||
        !service_gui_theme_uses_glass())
    {
        return;
    }

    (void)service_gui_main_background_refresh_tabs();
}

/**
 * @brief 为 MainPageContainer 安装局部背景同步回调。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY MainPageContainer 尚未创建。
 * @note 同一 Viewport 只注册一次。Pager 也会向同一对象注册独立的
 *       LV_EVENT_SCROLL_END 回调；两者只共享 LVGL 事件源，不共享状态。
 */
static Service_StatusTypeDef service_gui_main_background_bind_scroll_event(void)
{
    if (service_gui_main_background_view->main.page_container == NULL)
    {
        return SERVICE_NOT_READY;
    }

    if (service_gui_main_background_view->main.page_container != service_gui_main_background_page_container)
    {
        lv_obj_add_event_cb(
            service_gui_main_background_view->main.page_container,
            service_gui_main_background_page_scroll_event,
            LV_EVENT_SCROLL,
            NULL);
        service_gui_main_background_page_container = service_gui_main_background_view->main.page_container;
    }

    return SERVICE_OK;
}

/**
 * @brief 从清晰壁纸生成 Main 长期持有的全屏模糊壁纸。
 * @retval SERVICE_OK 成功，或此前已生成。
 * @retval SERVICE_INVALID_PARAM 壁纸图片或缓冲不满足 Canvas 约束。
 * @note 借用 Canvas 共享工作区后立即复制到本 Module 缓冲。只在首次需要毛玻璃时
 *       执行一次：Default 上电时在 Boot 占用工作区之前；Solid 上电后运行时切到
 *       Default 时，Boot 不曾绑定工作区，借用同样安全。
 */
static Service_StatusTypeDef service_gui_main_background_build_glass(void)
{
    Service_StatusTypeDef status;
    lv_img_dsc_t *blurred_wallpaper;
    lv_area_t full_wallpaper_area;

    if (service_gui_main_background_blurred_wallpaper_ready)
    {
        return SERVICE_OK;
    }

    status = service_gui_canvas_blur_image(
        service_gui_main_background_clear_wallpaper,
        SERVICE_GUI_MAIN_BACKGROUND_WALLPAPER_BLUR_RADIUS,
        &blurred_wallpaper);

    if (status != SERVICE_OK)
    {
        return status;
    }

    full_wallpaper_area.x1 = 0;
    full_wallpaper_area.y1 = 0;
    full_wallpaper_area.x2 = (lv_coord_t)blurred_wallpaper->header.w - 1;
    full_wallpaper_area.y2 = (lv_coord_t)blurred_wallpaper->header.h - 1;

    status = service_gui_canvas_extract_image_region(
        blurred_wallpaper,
        &full_wallpaper_area,
        service_gui_main_background_blurred_wallpaper_buffer,
        sizeof(service_gui_main_background_blurred_wallpaper_buffer),
        &service_gui_main_background_blurred_wallpaper);

    if (status != SERVICE_OK)
    {
        return status;
    }

    service_gui_main_background_blurred_wallpaper_ready = true;
    return SERVICE_OK;
}

/**
 * @brief 按当前外观设置 MusicModeTabs 背景：Default 毛玻璃，Solid 半透明 Wash。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY 尚未 prepare，或 Main/Tabview 尚未创建。
 * @retval SERVICE_INVALID_PARAM 首次生成毛玻璃时壁纸或裁剪缓冲不满足约束。
 * @note 只能由 GUI Task 调用。首次切到 Default 时按需生成长期模糊壁纸，之后切换
 *       只换背景源，不重复模糊。
 */
Service_StatusTypeDef service_gui_main_background_apply(void)
{
    Service_StatusTypeDef status;
    lv_obj_t *tabs;

    if ((service_gui_main_background_view == NULL) ||
        (service_gui_main_background_clear_wallpaper == NULL) ||
        (service_gui_main_background_view->music.tabs == NULL))
    {
        return SERVICE_NOT_READY;
    }

    tabs = service_gui_main_background_view->music.tabs;

    if (!service_gui_theme_uses_glass())
    {
        lv_obj_set_style_bg_img_src(tabs, NULL, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_img_opa(tabs, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(tabs, SERVICE_GUI_THEME_SOLID_TABS_WASH_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_invalidate(tabs);
        return SERVICE_OK;
    }

    status = service_gui_main_background_build_glass();

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = service_gui_main_background_refresh_tabs();

    if (status != SERVICE_OK)
    {
        return status;
    }

    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(tabs, &service_gui_main_background_tabs_image, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_opa(tabs, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_invalidate(tabs);

    return SERVICE_OK;
}

/**
 * @brief 记录壁纸、绑定滚动同步，并按当前外观设置 MusicModeTabs 背景。
 * @param clear_wallpaper 当前清晰系统壁纸。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY Main Screen、分页视口或 Tabview 尚未就绪。
 * @retval SERVICE_INVALID_PARAM 壁纸图片、布局区域或裁剪缓冲不满足 Canvas 约束。
 * @note 调用前 Pager 必须已完成布局和初始回中。Books 与 Settings 后续各自拥有
 *       背景与生命周期，不能在此处复用 Music 的运行时缓冲。
 */
Service_StatusTypeDef service_gui_main_background_prepare(
    const lv_img_dsc_t *clear_wallpaper)
{
    Service_StatusTypeDef status;

    service_gui_main_background_view = service_gui_view_get();
    service_gui_main_background_clear_wallpaper = clear_wallpaper;

    if ((clear_wallpaper == NULL) ||
        (service_gui_main_background_view->main.screen == NULL) ||
        (service_gui_main_background_view->main.page_container == NULL) ||
        (service_gui_main_background_view->music.tabs == NULL))
    {
        return SERVICE_NOT_READY;
    }

    status = service_gui_main_background_bind_scroll_event();

    if (status != SERVICE_OK)
    {
        return status;
    }

    return service_gui_main_background_apply();
}
