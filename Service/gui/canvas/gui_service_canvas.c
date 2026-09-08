/**
  ******************************************************************************
  * @file    gui_service_canvas.c
  * @brief   GUI Service 的共享 Canvas 离屏处理实现。
  *
  * @details
 *          本 Module 持有全屏模糊工作区和一块独立的 Now Playing 假唱盘缓冲。
 *          模糊路径把全屏真彩图复制到 SDRAM 后做横/纵软件模糊；描述符指向可复用
 *          工作区，Boot 可直接绑定，Main 必须先复制。假唱盘路径把纯色圆写入独立
 *          缓冲，可长期绑到 SquareLine Image，且不会被模糊覆写。
  ******************************************************************************
  */

#include "Service/gui/canvas/gui_service_canvas.h"
#include "Service/gui/canvas/gui_service_canvas_compositor.h"
#include "Service/gui/canvas/gui_service_canvas_config.h"
#include "Service/gui/gui_service.h"

#include <stdint.h>

#if SERVICE_GUI_MUSIC_VINYL_COVER_DIAMETER > SERVICE_GUI_MUSIC_VINYL_DIAMETER
#error "假封面直径必须小于或等于唱盘直径。"
#endif


/**
 * @brief GUI 离屏视觉效果的可复用 Canvas 像素工作区。
 * @note  当前按全屏真彩带 Alpha 图像的最大尺寸分配，位于外部 SDRAM。
 *        GUI Task 同一时刻只允许一个离屏效果改写该工作区；新一次模糊会覆盖
 *        上一次返回的像素。
 */
static uint8_t service_gui_effect_canvas_buffer[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(
        SERVICE_GUI_WIDTH,
        SERVICE_GUI_HEIGHT)]
    __attribute__((section(".sdram_framebuffer"),
                   aligned(PLATFORM_DCACHE_LINE_SIZE)));

/**
 * @brief Now Playing 假唱盘合成像素缓冲。
 * @note  按唱盘直径分配 TRUE_COLOR_ALPHA，位于外部 SDRAM。与全屏模糊工作区独立；
 *        绑定到 Image 后必须保持有效，不得被模糊路径覆写。
 */
static uint8_t service_gui_music_vinyl_canvas_buffer[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(
        SERVICE_GUI_MUSIC_VINYL_DIAMETER,
        SERVICE_GUI_MUSIC_VINYL_DIAMETER)]
    __attribute__((section(".sdram_framebuffer"),
                   aligned(PLATFORM_DCACHE_LINE_SIZE)));

/** @brief 绑定假唱盘缓冲的图片描述符。 */
static lv_img_dsc_t service_gui_music_vinyl_image;

/**
 * @brief 与共享工作区绑定的运行时 Canvas 对象。
 * @note  它挂在 display 的 top layer，而不挂在短生命周期的 Boot Screen，保证
 *        后续更换壁纸或生成 Settings 局部毛玻璃时仍可安全复用。
 */
static lv_obj_t *service_gui_canvas;

/**
 * @brief 面向调用方的完整模糊图描述符。
 * @note LVGL v8 的 lv_canvas_set_buffer() 不会维护 Canvas 内部描述符的
 *       data_size。此处从 Canvas 描述符复制格式、尺寸和数据地址后补齐该
 *       字段，保证它可作为普通 lv_img_dsc_t 传递给局部合成器。
 */
static lv_img_dsc_t service_gui_effect_blurred_image;

/**
 * @brief 使用共享 Canvas 生成一张全屏图片的模糊副本。
 * @param source 需要模糊的全屏真彩图片描述符。
 * @param blur_radius 横向和纵向模糊半径，必须非零。
 * @param blurred_image 返回带完整元数据的共享 Canvas 图片描述符。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_INVALID_PARAM 图片尺寸、格式或数据大小不满足当前原型约束。
 * @retval SERVICE_NOT_READY LVGL Canvas 对象尚不可用。
 * @note 输出描述符及其像素数据在下次调用本函数前有效。当前只允许一项离屏视觉
 *       效果改写该共享 Canvas；若调用方需长期保留结果，必须自行复制像素与描述符。
 */
Service_StatusTypeDef service_gui_canvas_blur_image(
    const lv_img_dsc_t *source,
    uint16_t blur_radius,
    lv_img_dsc_t **blurred_image)
{
    uint32_t required_bytes;

    if ((source == NULL) || (blurred_image == NULL) || (blur_radius == 0U))
    {
        return SERVICE_INVALID_PARAM;
    }

    if ((source->data == NULL) ||
        (source->header.w != PLATFORM_LCD_WIDTH) ||
        (source->header.h != PLATFORM_LCD_HEIGHT) ||
        ((source->header.cf != LV_IMG_CF_TRUE_COLOR) &&
         (source->header.cf != LV_IMG_CF_TRUE_COLOR_ALPHA)))
    {
        return SERVICE_INVALID_PARAM;
    }

    required_bytes = lv_img_buf_get_img_size(
        source->header.w,
        source->header.h,
        source->header.cf);

    if ((source->data_size != required_bytes) ||
        (required_bytes > sizeof(service_gui_effect_canvas_buffer)))
    {
        return SERVICE_INVALID_PARAM;
    }

    if (service_gui_canvas == NULL)
    {
        service_gui_canvas = lv_canvas_create(lv_layer_top());

        if (service_gui_canvas == NULL)
        {
            return SERVICE_NOT_READY;
        }

        lv_obj_add_flag(service_gui_canvas, LV_OBJ_FLAG_HIDDEN);
    }

    lv_canvas_set_buffer(
        service_gui_canvas,
        service_gui_effect_canvas_buffer,
        source->header.w,
        source->header.h,
        source->header.cf);

    lv_canvas_copy_buf(
        service_gui_canvas,
        source->data,
        0,
        0,
        source->header.w,
        source->header.h);

    lv_canvas_blur_hor(
        service_gui_canvas,
        NULL,
        blur_radius);

    lv_canvas_blur_ver(
        service_gui_canvas,
        NULL,
        blur_radius);

    service_gui_effect_blurred_image = *lv_canvas_get_img(service_gui_canvas);
    service_gui_effect_blurred_image.data_size = required_bytes;

    *blurred_image = &service_gui_effect_blurred_image;
    lv_img_cache_invalidate_src(*blurred_image);

    return SERVICE_OK;
}

/**
 * @brief 用两块纯色圆合成假唱盘，并返回绑定内部缓冲的描述符。
 * @param[out] image 返回假唱盘图片描述符。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_INVALID_PARAM image 为空，或合成参数不满足约束。
 * @note 输出像素指向本 Module 的唱盘缓冲，可长期绑到 SquareLine Image。
 *       不读 ID3，不导入 PNG。
 */
Service_StatusTypeDef service_gui_canvas_compose_music_vinyl_fake(
    lv_img_dsc_t **image)
{
    Service_StatusTypeDef status;

    if (image == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }

    status = service_gui_canvas_compose_solid_circles(
        SERVICE_GUI_MUSIC_VINYL_DIAMETER,
        SERVICE_GUI_MUSIC_VINYL_FAKE_DISC_RGB,
        SERVICE_GUI_MUSIC_VINYL_COVER_DIAMETER,
        SERVICE_GUI_MUSIC_VINYL_FAKE_COVER_RGB,
        service_gui_music_vinyl_canvas_buffer,
        (uint32_t)sizeof(service_gui_music_vinyl_canvas_buffer),
        &service_gui_music_vinyl_image);

    if (status != SERVICE_OK)
    {
        return status;
    }

    *image = &service_gui_music_vinyl_image;
    lv_img_cache_invalidate_src(*image);

    return SERVICE_OK;
}
