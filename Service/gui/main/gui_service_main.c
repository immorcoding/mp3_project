/**
  ******************************************************************************
  * @file    gui_service_main.c
 * @brief   Main Screen 运行时局部毛玻璃与 Tabview 内部 Content 兼容实现。
  *
  * @details
 *          本 Module 不修改 SquareLine 导出的视觉设计。它在布局完成后读取导出
 *          对象的实际屏幕坐标，以 Main Screen 为原点换算为壁纸图坐标；随后
 *          将整块 MusicModeTabs 和三颗控制按钮的局部模糊像素合成到一张长期
 *          SDRAM 背景图中，并仅将该运行时背景绑定到 ui_Main 根对象。除此
 *          以外，本 Module 只处理 SquareLine 无法访问的 Tabview 内部 Content
 *          container；该背景只在壁纸或布局改变时重建，绝不在每帧 GUI 处理
 *          路径中计算。
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
 * @brief Main Screen 长期持有的局部毛玻璃合成背景像素缓冲。
 * @note 当前壁纸为 LV_IMG_CF_TRUE_COLOR_ALPHA，因此按同格式分配。它不由
 *       Canvas 工作区持有，后者可在本次合成结束后继续服务其他离屏效果。
 */
static uint8_t service_gui_main_composite_background_buffer[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(
        PLATFORM_LCD_WIDTH,
        PLATFORM_LCD_HEIGHT)]
    __attribute__((section(".sdram_framebuffer"),
                   aligned(PLATFORM_DMA_BUFFER_ALIGNMENT)));

/** @brief 绑定 Main 根背景的长期 LVGL 图片描述符。 */
static lv_img_dsc_t service_gui_main_composite_background;

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
 * @brief 按 Main Screen 原点将一个对象转换为 Canvas 模糊区域描述。
 * @param object SquareLine 导出的目标对象。
 * @param main_area Main Screen 的实际屏幕区域。
 * @param shape 区域裁剪形状。
 * @param region 返回的壁纸内区域描述。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY 对象或输出区域为空。
 * @note 坐标来自布局后的对象实际区域，故 SquareLine 中调整相对位置、百分比尺寸
 *       或 MAIN Radius 后，无需同步维护任何 Service 内像素常量。
 */
static Service_StatusTypeDef service_gui_main_make_region_from_object(
    const lv_obj_t *object,
    const lv_area_t *main_area,
    Service_GUI_CanvasRegionShapeTypeDef shape,
    Service_GUI_CanvasBlurRegionTypeDef *region)
{
    lv_area_t object_area;

    if ((object == NULL) || (main_area == NULL) || (region == NULL))
    {
        return SERVICE_NOT_READY;
    }

    lv_obj_get_coords(object, &object_area);

    region->Area.x1 = object_area.x1 - main_area->x1;
    region->Area.y1 = object_area.y1 - main_area->y1;
    region->Area.x2 = object_area.x2 - main_area->x1;
    region->Area.y2 = object_area.y2 - main_area->y1;
    region->Radius = (uint16_t)lv_obj_get_style_radius(
        object,
        LV_PART_MAIN | LV_STATE_DEFAULT);
    region->Shape = shape;

    return SERVICE_OK;
}

/**
 * @brief 为 Main 根背景生成局部毛玻璃并消除 Tabview 的主题默认白底。
 * @param clear_wallpaper 当前清晰系统壁纸。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_NOT_READY Main Screen、Tabview 或目标控件尚未就绪。
 * @retval SERVICE_INVALID_PARAM 壁纸图片或布局区域不满足 Canvas 合成约束。
 * @note 当前只构建 MusicPage 的合成背景。Books 与 Settings 后续各自拥有页面级
 *       合成背景或切页前的重建时机，不能在此处静态复用 Music 的区域列表。
 */
Service_StatusTypeDef service_gui_main_prepare_background(
    const lv_img_dsc_t *clear_wallpaper)
{
    Service_GUI_CanvasBlurRegionTypeDef glass_regions[4];
    Service_StatusTypeDef status;
    lv_img_dsc_t *blurred_wallpaper;
    lv_area_t main_area;

    if ((ui_Main == NULL) ||
        (ui_MainPager == NULL) ||
        (ui_MusicModeTabs == NULL) ||
        (ui_MusicPreviousButton == NULL) ||
        (ui_MusicPlayPauseButton == NULL) ||
        (ui_MusicNextButton == NULL))
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

    status = service_gui_main_make_region_from_object(
        ui_MusicModeTabs,
        &main_area,
        SERVICE_GUI_CANVAS_REGION_ROUNDED_RECT,
        &glass_regions[0]);

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = service_gui_main_make_region_from_object(
        ui_MusicPreviousButton,
        &main_area,
        SERVICE_GUI_CANVAS_REGION_CIRCLE,
        &glass_regions[1]);

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = service_gui_main_make_region_from_object(
        ui_MusicPlayPauseButton,
        &main_area,
        SERVICE_GUI_CANVAS_REGION_CIRCLE,
        &glass_regions[2]);

    if (status != SERVICE_OK)
    {
        return status;
    }

    status = service_gui_main_make_region_from_object(
        ui_MusicNextButton,
        &main_area,
        SERVICE_GUI_CANVAS_REGION_CIRCLE,
        &glass_regions[3]);

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

    status = service_gui_canvas_compose_blurred_regions(
        clear_wallpaper,
        blurred_wallpaper,
        glass_regions,
        sizeof(glass_regions) / sizeof(glass_regions[0]),
        service_gui_main_composite_background_buffer,
        sizeof(service_gui_main_composite_background_buffer),
        &service_gui_main_composite_background);

    if (status != SERVICE_OK)
    {
        return status;
    }

    lv_img_cache_invalidate_src(&service_gui_main_composite_background);
    lv_obj_set_style_bg_img_src(
        ui_Main,
        &service_gui_main_composite_background,
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_opa(
        ui_Main,
        LV_OPA_COVER,
        LV_PART_MAIN | LV_STATE_DEFAULT);

    return SERVICE_OK;
}
