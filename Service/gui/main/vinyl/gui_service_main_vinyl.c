/**
 ******************************************************************************
 * @file    gui_service_main_vinyl.c
 * @brief   把 Canvas 唱盘第一帧绑到 MusicPlayerVinylImage，并按 playing 旋转。
 *
 * @details
 *          不读 ID3、不导入 PNG、不读播放游标。底图来自 Resource 加载到 SDRAM
 *          槽的 ID 4；Canvas 复制后再叠假封面。本 Module 核对 Image 尺寸并
 *          set_src，之后由 apply 决定唱盘是否绕中心顺时针匀速旋转：角度只存在
 *          lv_img 对象里，动画以该对象为 var，所以同一 Image 任一时刻至多一条
 *          lv_anim；暂停删动画即停在当前角度，恢复从该角度续转。
 ******************************************************************************
 */

#include "Service/gui/main/vinyl/gui_service_main_vinyl.h"
#include "Service/gui/main/vinyl/gui_service_main_vinyl_config.h"

#include <stdint.h>

#include "Service/gui/canvas/gui_service_canvas.h"
#include "Service/gui/gui_service.h"

#include "Service/gui/view/gui_service_view.h"
#include "lvgl.h"

/** @brief 链接器为唱盘底图数据预留的 SDRAM 起点。 */
extern uint8_t __external_resource_vinyl_start__[];
/** @brief 链接器为唱盘底图数据预留的 SDRAM 终点。 */
extern uint8_t __external_resource_vinyl_end__[];

/** @brief prepare 成功后持有的唱盘 Image；Main Screen 不销毁，故不会失效。 */
static lv_obj_t *service_gui_main_vinyl_image;

static void service_gui_main_vinyl_anim_exec(void *var, int32_t value);

/**
 * @brief 旋转动画每帧回调：把累计角度折回一圈内写给唱盘 Image。
 * @param[in] var 动画 var，即唱盘 Image。
 * @param[in] value 自起转角度累加的 0.1° 值，一轮内不超过两圈。
 * @note 仅由 LVGL 动画定时器在 GUI Task 的 lv_timer_handler() 内调用。
 */
static void service_gui_main_vinyl_anim_exec(void *var, int32_t value)
{
    lv_img_set_angle(
        (lv_obj_t *)var,
        (int16_t)(value % SERVICE_GUI_MAIN_VINYL_FULL_TURN));
}

/**
 * @brief 用 Resource 底图合成唱盘并绑到 Now Playing Image，角度置 0、不起转。
 * @retval SERVICE_OK 第一帧已可见。
 * @retval SERVICE_NOT_READY 唱盘 Image 尚未创建。
 * @retval SERVICE_INVALID_PARAM 对象尺寸与唱盘直径宏不一致，槽容量不匹配，或合成失败。
 * @note 仅由 service_gui_main_prepare() 在 Transport 之后、Background 之前调用一次。
 *       旋转由 service_gui_main_vinyl_apply() 按 playing 开关。
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
    /* 绕圆心转；角度 0 时 LVGL 不走变换路径，第一帧像素与不旋转时相同。 */
    lv_img_set_pivot(
        image,
        (lv_coord_t)(SERVICE_GUI_MUSIC_VINYL_DIAMETER / 2U),
        (lv_coord_t)(SERVICE_GUI_MUSIC_VINYL_DIAMETER / 2U));
    lv_img_set_angle(image, 0);
    service_gui_main_vinyl_image = image;
    return SERVICE_OK;
}

/**
 * @brief 按 playing 起转或停转唱盘；reset_angle 先把角度归零。
 * @param[in] playing 为真从当前角度顺时针续转，每 SERVICE_GUI_MAIN_VINYL_REVOLUTION_MS 一圈；
 *            为假停在当前角度。
 * @param[in] reset_angle 为真先归零（切歌、清空），再按 playing 决定是否起转。
 * @retval SERVICE_OK 动画状态已与参数一致。
 * @retval SERVICE_NOT_READY 唱盘尚未 prepare。
 * @note 只能由 GUI Task 调用。先删旧动画再决定起转，因此反复 play/pause 不会叠加动画；
 *       playing 不变时重复调用只会从当前角度重新起转，不跳变。不分配堆外内存：lv_anim
 *       由 LVGL 在删除或对象销毁时回收。
 */
Service_StatusTypeDef service_gui_main_vinyl_apply(bool playing, bool reset_angle)
{
    lv_anim_t anim;
    int32_t start;

    if (service_gui_main_vinyl_image == NULL)
    {
        return SERVICE_NOT_READY;
    }

    (void)lv_anim_del(service_gui_main_vinyl_image, service_gui_main_vinyl_anim_exec);

    if (reset_angle)
    {
        lv_img_set_angle(service_gui_main_vinyl_image, 0);
    }

    if (!playing)
    {
        return SERVICE_OK;
    }

    start = (int32_t)lv_img_get_angle(service_gui_main_vinyl_image);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, service_gui_main_vinyl_image);
    lv_anim_set_exec_cb(&anim, service_gui_main_vinyl_anim_exec);
    lv_anim_set_values(&anim, start, start + SERVICE_GUI_MAIN_VINYL_FULL_TURN);
    lv_anim_set_time(&anim, SERVICE_GUI_MAIN_VINYL_REVOLUTION_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim);

    return SERVICE_OK;
}
