/**
  ******************************************************************************
  * @file    gui_service_main.c
  * @brief   Main Screen 运行时局部毛玻璃与 Tabview 内部 Content 兼容实现。
  *
  * @details
  *          本 Module 不修改 SquareLine 导出的视觉设计。它在布局完成后读取导出
  *          对象的实际屏幕坐标，以 Main Screen 为原点换算为壁纸图坐标；随后
  *          从全屏模糊帧裁剪出与 MusicModeTabs 完全等大的局部背景，并仅将该
  *          运行时 Background image 绑定到 ui_MusicModeTabs。该图片随 Tabview
  *          与 MainPager 一起移动；除此以外，本 Module 只处理 SquareLine 无法
  *          访问的 Tabview 内部 Content container。裁剪图只在壁纸或布局改变时
  *          重建，绝不在每帧 GUI 处理路径中计算。
  ******************************************************************************
  */

#include "Service/gui/main/gui_service_main.h"
#include "Service/gui/main/gui_service_main_config.h"

#include <stdint.h>

#include "Platform/lcd/platform_lcd.h"
#include "Service/gui/canvas/gui_service_canvas.h"
#include "Service/gui/canvas/gui_service_canvas_compositor.h"

#include "GUI/ui.h"

/**
 * @brief Main Screen 长期持有的 MusicModeTabs 局部毛玻璃裁剪像素缓冲。
 * @note 当前壁纸为 LV_IMG_CF_TRUE_COLOR_ALPHA，因此按同格式分配。它不由
 *       Canvas 工作区持有，后者可在本次裁剪结束后继续服务其他离屏效果。
 */
static uint8_t service_gui_main_tabs_background_buffer[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(
        SERVICE_GUI_MAIN_TABS_BACKGROUND_MAX_WIDTH,
        SERVICE_GUI_MAIN_TABS_BACKGROUND_MAX_HEIGHT)]
    __attribute__((section(".sdram_framebuffer"),
                   aligned(PLATFORM_DMA_BUFFER_ALIGNMENT)));

/** @brief 绑定 MusicModeTabs 局部背景的长期 LVGL 图片描述符。 */
static lv_img_dsc_t service_gui_main_tabs_background;

/**
 * @brief 清除 LVGL 内部 Content container 的默认视觉层。
 * @param object 仅限由 LVGL 内部创建、SquareLine 未导出的 Content container。
 * @note 该函数不得用于 SquareLine 导出的对象。Border、Outline、Shadow、Radius
 *       等根对象视觉设计完全由 SquareLine 管理；此处只处理用户无法在
 *       SquareLine Inspector 中访问的 Tabview 内部 Content container。
 */
static void service_gui_main_make_internal_content_transparent(lv_obj_t *object)
{
    lv_obj_set_style_bg_opa(
        object,
        LV_OPA_TRANSP,
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_opa(
        object,
        LV_OPA_TRANSP,
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(
        object,
        0,
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(
        object,
        0,
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(
        object,
        0,
        LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * @brief 使一个 SquareLine Tabview 的 LVGL 内部 Content container 透明。
 * @param tabview SquareLine 导出的 Tabview 对象。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY Tabview 或其内部 Content container 尚未创建。
 * @note SquareLine v1.6.1 未暴露 Tabview 的内部 Content container 样式；该对象
 *       在 Simplified Theme 下默认具有不透明白底，必须由 GUI Service 补充处理。
 *       Tabview 本体仍完整保留 SquareLine 导出的 Border、Shadow、Radius 和背景
 *       设计，不能在本 Module 中覆盖。
 */
static Service_StatusTypeDef service_gui_main_make_tabview_content_transparent(
    lv_obj_t *tabview)
{
    lv_obj_t *content;

    if (tabview == NULL)
    {
        return SERVICE_NOT_READY;
    }

    content = lv_tabview_get_content(tabview);

    if (content == NULL)
    {
        return SERVICE_NOT_READY;
    }

    service_gui_main_make_internal_content_transparent(content);

    return SERVICE_OK;
}

/**
 * @brief 按 Main Screen 原点将对象坐标转换为壁纸内裁剪区域。
 * @param object SquareLine 导出的目标对象。
 * @param main_area Main Screen 的实际屏幕区域。
 * @param image_area 返回的壁纸内闭区间裁剪区域。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY 对象或输出区域为空。
 * @note 坐标来自布局后的对象实际区域，故 SquareLine 中调整相对位置、百分比尺寸
 *       或 MAIN Radius 后，无需同步维护任何 Service 内像素常量。输出区域的尺寸
 *       必须不超过 gui_service_main_config.h 中的局部背景容量上限。
 */
static Service_StatusTypeDef service_gui_main_get_object_image_area(
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
 * @brief 为 MusicModeTabs 生成随对象移动的局部毛玻璃并消除主题默认白底。
 * @param clear_wallpaper 当前清晰系统壁纸。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY Main Screen、Tabview 或目标控件尚未就绪。
 * @retval SERVICE_INVALID_PARAM 壁纸图片、布局区域或裁剪缓冲不满足 Canvas 约束。
 * @note 当前只构建 MusicModeTabs 的局部裁剪图。Books 与 Settings 后续各自拥有
 *       页面级裁剪图或切页前的重建时机，不能在此处静态复用 Music 的对象区域。
 */
Service_StatusTypeDef service_gui_main_prepare_background(
    const lv_img_dsc_t *clear_wallpaper)
{
    Service_StatusTypeDef status;
    lv_img_dsc_t *blurred_wallpaper;
    lv_area_t main_area;
    lv_area_t tabs_image_area;

    if ((ui_Main == NULL) ||
        (ui_MainPager == NULL) ||
        (ui_MusicModeTabs == NULL))
    {
        return SERVICE_NOT_READY;
    }

    status = service_gui_main_make_tabview_content_transparent(ui_MainPager);

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = service_gui_main_make_tabview_content_transparent(ui_MusicModeTabs);

    if (status != SERVICE_OK)
    {
        return status;
    }

    /* 先触发布局，确保百分比尺寸与 Flex 布局均已解析为实际坐标。 */
    lv_obj_update_layout(ui_Main);
    lv_obj_get_coords(ui_Main, &main_area);

    status = service_gui_main_get_object_image_area(
        ui_MusicModeTabs,
        &main_area,
        &tabs_image_area);

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = service_gui_canvas_blur_image(
        clear_wallpaper,
        SERVICE_GUI_MAIN_WALLPAPER_BLUR_RADIUS,
        &blurred_wallpaper);

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = service_gui_canvas_extract_image_region(
        blurred_wallpaper,
        &tabs_image_area,
        service_gui_main_tabs_background_buffer,
        sizeof(service_gui_main_tabs_background_buffer),
        &service_gui_main_tabs_background);

    if (status != SERVICE_OK)
    {
        return status;
    }

    lv_img_cache_invalidate_src(&service_gui_main_tabs_background);
    lv_obj_set_style_bg_img_src(
        ui_MusicModeTabs,
        &service_gui_main_tabs_background,
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_opa(
        ui_MusicModeTabs,
        LV_OPA_COVER,
        LV_PART_MAIN | LV_STATE_DEFAULT);

    return SERVICE_OK;
}
