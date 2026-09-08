/**
 ******************************************************************************
 * @file    gui_service_main_vinyl.c
 * @brief   把 Canvas 假唱盘第一帧绑到 MusicPlayerVinylImage。
 *
 * @details
 *          不旋转、不读 ID3、不导入 PNG。唱盘像素由 canvas/ 合成后长期指向
 *          其独立缓冲；本 Module 只负责核对 SquareLine 槽位并 set_src。
 ******************************************************************************
 */

#include "Service/gui/main/vinyl/gui_service_main_vinyl.h"

#include "Service/gui/canvas/gui_service_canvas.h"
#include "Service/gui/gui_service.h"

#include "GUI/ui.h"
#include "lvgl.h"

/**
 * @brief 合成假唱盘并绑到 Now Playing Image。
 * @retval SERVICE_OK 第一帧已可见。
 * @retval SERVICE_NOT_READY SquareLine Image 尚未导出。
 * @retval SERVICE_INVALID_PARAM 对象尺寸与唱盘直径宏不一致，或合成失败。
 * @note 仅由 service_gui_main_prepare() 在 Transport 之后、Background 之前调用一次。
 *       本刀不加旋转动画。
 */
Service_StatusTypeDef service_gui_main_vinyl_prepare(void)
{
    lv_img_dsc_t *vinyl_image;
    Service_StatusTypeDef status;

    if (ui_MusicPlayerVinylImage == NULL)
    {
        return SERVICE_NOT_READY;
    }

    if ((lv_obj_get_width(ui_MusicPlayerVinylImage) !=
         (lv_coord_t)SERVICE_GUI_MUSIC_VINYL_DIAMETER) ||
        (lv_obj_get_height(ui_MusicPlayerVinylImage) !=
         (lv_coord_t)SERVICE_GUI_MUSIC_VINYL_DIAMETER))
    {
        return SERVICE_INVALID_PARAM;
    }

    lv_obj_clear_flag(
        ui_MusicPlayerVinylImage,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    status = service_gui_canvas_compose_music_vinyl_fake(&vinyl_image);

    if (status != SERVICE_OK)
    {
        return status;
    }

    lv_img_set_src(ui_MusicPlayerVinylImage, vinyl_image);
    return SERVICE_OK;
}
