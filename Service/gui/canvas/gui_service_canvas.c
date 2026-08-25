/**
  ******************************************************************************
  * @file    gui_service_canvas.c
  * @brief   GUI Service 的共享 Canvas 离屏处理实现。
  *
  * @details
  *          本 Module 持有唯一的全屏 Canvas 工作区。它将一张全屏 LVGL 真彩
  *          图像复制至外部 SDRAM 后执行横向、纵向软件模糊，并将 Canvas 的
  *          图像描述符返回给调用方绑定为运行时背景。
  ******************************************************************************
  */

#include "Service/gui/canvas/gui_service_canvas.h"

#include <stdint.h>

#include "Platform/lcd/platform_lcd.h"

/**
 * @brief GUI 离屏视觉效果的可复用 Canvas 像素工作区。
 * @note  当前按全屏真彩带 Alpha 图像的最大尺寸分配，位于外部 SDRAM。
 *        GUI Task 同一时刻只允许一个离屏效果占用该工作区。
 */
static uint8_t service_gui_effect_canvas_buffer[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(
        PLATFORM_LCD_WIDTH,
        PLATFORM_LCD_HEIGHT)]
    __attribute__((section(".sdram_framebuffer"),
                   aligned(PLATFORM_DMA_BUFFER_ALIGNMENT)));

/**
 * @brief 与共享工作区绑定的运行时 Canvas 对象。
 * @note  它挂在 display 的 top layer，而不挂在短生命周期的 Boot Screen，保证
 *        后续更换壁纸或生成 Settings 局部毛玻璃时仍可安全复用。
 */
static lv_obj_t *service_gui_canvas;

/**
 * @brief 使用共享 Canvas 生成一张全屏图片的模糊副本。
 * @param source 需要模糊的全屏真彩图片描述符。
 * @param blur_radius 横向和纵向模糊半径，必须非零。
 * @param blurred_image 返回共享 Canvas 的图片描述符。
 * @retval SERVICE_OK 成功。
 * @retval SERVICE_INVALID_PARAM 图片尺寸、格式或数据大小不满足当前原型约束。
 * @retval SERVICE_NOT_READY LVGL Canvas 对象尚不可用。
 * @note 输出描述符及其像素数据在下次调用本函数前有效。
 *       当前只允许一项离屏视觉效果占用该共享 Canvas。
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

    *blurred_image = lv_canvas_get_img(service_gui_canvas);
    lv_img_cache_invalidate_src(*blurred_image);

    return SERVICE_OK;
}
