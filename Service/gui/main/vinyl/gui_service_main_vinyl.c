/**
 ******************************************************************************
 * @file    gui_service_main_vinyl.c
 * @brief   把 Canvas 唱盘第一帧绑到 MusicPlayerVinylImage。
 *
 * @details
 *          不旋转、不读 ID3、不导入 PNG。底图来自 Resource 加载到 SDRAM 槽的
 *          ID 4；Canvas 复制后再叠假封面。本 Module 只负责核对 SquareLine 槽位
 *          并 set_src。
 ******************************************************************************
 */

#include "Service/gui/main/vinyl/gui_service_main_vinyl.h"

#include <stdint.h>

#include "Service/gui/canvas/gui_service_canvas.h"
#include "Service/gui/gui_service.h"

#include "Service/gui/view/gui_service_view.h"
#include "lvgl.h"

/** @brief 链接器为唱盘底图数据预留的 SDRAM 起点。 */
extern uint8_t __external_resource_vinyl_start__[];
/** @brief 链接器为唱盘底图数据预留的 SDRAM 终点。 */
extern uint8_t __external_resource_vinyl_end__[];

/**
 * @brief 用 Resource 底图合成唱盘并绑到 Now Playing Image。
 * @retval SERVICE_OK 第一帧已可见。
 * @retval SERVICE_NOT_READY SquareLine Image 尚未导出。
 * @retval SERVICE_INVALID_PARAM 对象尺寸与唱盘直径宏不一致，槽容量不匹配，或合成失败。
 * @note 仅由 service_gui_main_prepare() 在 Transport 之后、Background 之前调用一次。
 *       本刀不加旋转动画。
 */
Service_StatusTypeDef service_gui_main_vinyl_prepare(void)
{
    lv_obj_t *image = service_gui_view_get()->music.vinyl_image;
    lv_img_dsc_t base_image = {0};
    lv_img_dsc_t *vinyl_image;
    Service_StatusTypeDef status;
    uint32_t vinyl_bytes;
    uint32_t required_bytes;

    if (image == NULL)
    {
        return SERVICE_NOT_READY;
    }

    if ((lv_obj_get_width(image) !=
         (lv_coord_t)SERVICE_GUI_MUSIC_VINYL_DIAMETER) ||
        (lv_obj_get_height(image) !=
         (lv_coord_t)SERVICE_GUI_MUSIC_VINYL_DIAMETER))
    {
        return SERVICE_INVALID_PARAM;
    }

    vinyl_bytes = (uint32_t)(__external_resource_vinyl_end__ -
                             __external_resource_vinyl_start__);
    required_bytes = (uint32_t)SERVICE_GUI_MUSIC_VINYL_DIAMETER *
                     (uint32_t)SERVICE_GUI_MUSIC_VINYL_DIAMETER * 3U;

    if (vinyl_bytes != required_bytes)
    {
        return SERVICE_INVALID_PARAM;
    }

    lv_obj_clear_flag(
        image,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    base_image.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    base_image.header.w = SERVICE_GUI_MUSIC_VINYL_DIAMETER;
    base_image.header.h = SERVICE_GUI_MUSIC_VINYL_DIAMETER;
    base_image.data_size = required_bytes;
    base_image.data = __external_resource_vinyl_start__;

    status = service_gui_canvas_compose_music_vinyl(&base_image, &vinyl_image);

    if (status != SERVICE_OK)
    {
        return status;
    }

    lv_img_set_src(image, vinyl_image);
    return SERVICE_OK;
}
