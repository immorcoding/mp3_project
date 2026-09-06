/**
  ******************************************************************************
  * @file    gui_service_boot.c
  * @brief   GUI Service 启动视觉序列的实现入口。
  *
  * @details
  *          该 Module 在 SquareLine 静态 Boot Arc 之上叠加运行时动画。
  *          ui_init() 完成且模糊背景绑定后，GUI Task 显式进入本 Module，创建
  *          一组角度伸缩动画。该动画作用于普通 LVGL Arc，不改变 SquareLine
  *          导出的轨道样式和 Indicator 内缩 Padding。BootReveal 通过 SquareLine
  *          Call Function 进入本 Module；本 Module 使用 lv_async_call() 避免在
  *          前一次 Screen 切换的 SCREEN_LOADED 回调中嵌套发起下一次切换。
  ******************************************************************************
  */

#include "Service/gui/boot/gui_service_boot.h"
#include "Service/gui/boot/gui_service_boot_config.h"
#include "Service/gui/canvas/gui_service_canvas.h"
#include "Service/gui/theme/gui_service_theme.h"

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#include "GUI/ui.h"

#define SERVICE_GUI_BOOT_ARC_FULL_CIRCLE_DEG  (360U)
#define SERVICE_GUI_BOOT_ARC_PHASE_MAX        (LV_BEZIER_VAL_MAX)
#define SERVICE_GUI_BOOT_ARC_PHASE_HALF       (LV_BEZIER_VAL_MAX / 2U)

/** @brief 防止同一 BootReveal 事件重复提交异步切屏请求。 */
static bool service_gui_boot_lock_request_pending;

/** @brief 防止启动序列已进入 Lock 后再次发起切屏。 */
static bool service_gui_boot_lock_transition_started;

#if (SERVICE_GUI_BOOT_ARC_MIN_SWEEP_DEG >= SERVICE_GUI_BOOT_ARC_MAX_SWEEP_DEG)
#error "Boot Arc minimum sweep must be smaller than maximum sweep."
#endif

#if (SERVICE_GUI_BOOT_ARC_MAX_SWEEP_DEG >= SERVICE_GUI_BOOT_ARC_FULL_CIRCLE_DEG)
#error "Boot Arc maximum sweep must be smaller than a full circle."
#endif

#if (SERVICE_GUI_BOOT_ARC_START_OFFSET_DEG >= SERVICE_GUI_BOOT_ARC_FULL_CIRCLE_DEG)
#error "Boot Arc start offset must be in the range [0, 360)."
#endif

/**
 * @brief 把一个未偏移的 Arc 角度转换为屏幕显示角度。
 * @param angle 未偏移的 Arc 角度。
 * @return 加上固定逆时针偏移后的 0 至 359 度角度。
 */
static uint16_t service_gui_boot_apply_arc_offset(uint16_t angle)
{
    return (uint16_t)(
        (angle + SERVICE_GUI_BOOT_ARC_START_OFFSET_DEG) %
        SERVICE_GUI_BOOT_ARC_FULL_CIRCLE_DEG);
}

/**
 * @brief 计算标准缓入缓出的 0 至 LV_BEZIER_VAL_MAX 相位进度。
 * @param progress 线性进度，范围为 0 至 LV_BEZIER_VAL_MAX。
 * @return 使用 LVGL 默认 ease-in-out 控制点计算后的进度。
 */
static uint16_t service_gui_boot_ease_in_out(uint16_t progress)
{
    return (uint16_t)lv_bezier3(
        progress,
        0U,
        180U,
        844U,
        LV_BEZIER_VAL_MAX);
}

/**
 * @brief 按统一相位设置活动弧的两个端点。
 * @param target SquareLine 导出的 Boot Arc。
 * @param value 线性循环相位，范围为 0 至 LV_BEZIER_VAL_MAX。
 * @details
 *          前半周期让前端加速向前而后端匀速前进，以延长活动弧；后半周期保持
 *          前端匀速前进，让后端加速追赶，以缩短活动弧。两个端点均不会反向移动。
 *          周期末的几何状态与下一周期起点完全相同，因此不会在循环边界跳变。
 */
static void service_gui_boot_set_arc_phase(void *target, int32_t value)
{
    const uint16_t sweep_range =
        SERVICE_GUI_BOOT_ARC_MAX_SWEEP_DEG -
        SERVICE_GUI_BOOT_ARC_MIN_SWEEP_DEG;
    const uint16_t cycle_advance =
        SERVICE_GUI_BOOT_ARC_FULL_CIRCLE_DEG - sweep_range;
    uint16_t phase;
    uint16_t base_angle;
    uint16_t sweep_angle;
    uint16_t start_angle;
    uint16_t end_angle;
    uint16_t eased_progress;

    if (value <= 0)
    {
        phase = 0U;
    }
    else if ((uint32_t)value >= SERVICE_GUI_BOOT_ARC_PHASE_MAX)
    {
        phase = SERVICE_GUI_BOOT_ARC_PHASE_MAX;
    }
    else
    {
        phase = (uint16_t)value;
    }

    base_angle = (uint16_t)(
        ((uint32_t)cycle_advance * phase) /
        SERVICE_GUI_BOOT_ARC_PHASE_MAX);

    if (phase <= SERVICE_GUI_BOOT_ARC_PHASE_HALF)
    {
        eased_progress = service_gui_boot_ease_in_out(
            (uint16_t)(phase * 2U));
        sweep_angle = (uint16_t)(
            SERVICE_GUI_BOOT_ARC_MIN_SWEEP_DEG +
            (((uint32_t)sweep_range * eased_progress) /
             SERVICE_GUI_BOOT_ARC_PHASE_MAX));
        start_angle = base_angle;
        end_angle = (uint16_t)(base_angle + sweep_angle);
    }
    else
    {
        eased_progress = service_gui_boot_ease_in_out(
            (uint16_t)((phase - SERVICE_GUI_BOOT_ARC_PHASE_HALF) * 2U));
        sweep_angle = (uint16_t)(
            SERVICE_GUI_BOOT_ARC_MAX_SWEEP_DEG -
            (((uint32_t)sweep_range * eased_progress) /
             SERVICE_GUI_BOOT_ARC_PHASE_MAX));
        start_angle = (uint16_t)(
            base_angle +
            (SERVICE_GUI_BOOT_ARC_MAX_SWEEP_DEG - sweep_angle));
        end_angle = (uint16_t)(
            base_angle +
            SERVICE_GUI_BOOT_ARC_MAX_SWEEP_DEG);
    }

    lv_arc_set_angles(
        (lv_obj_t *)target,
        service_gui_boot_apply_arc_offset(start_angle),
        service_gui_boot_apply_arc_offset(end_angle));
}

/**
 * @brief 为 Boot Screen 生成并绑定当前清晰壁纸的模糊背景。
 * @param clear_wallpaper 当前系统壁纸的清晰图片描述符。
 * @retval SERVICE_OK 成功；Solid 下跳过模糊并保持 ThemeApply 的 Ground。
 * @retval SERVICE_NOT_READY Boot Screen 或 Canvas 工作对象尚不可用。
 * @retval SERVICE_INVALID_PARAM 壁纸格式或尺寸不满足当前 Canvas 原型约束。
 * @note  Default 才绑定 Canvas 共享工作帧。Solid 不得再写 Background image，
 *        否则会盖掉 ThemeApply 关掉的壁纸。Boot 显示期间不能再次调用 Canvas
 *        模糊 Interface。
 */
Service_StatusTypeDef service_gui_boot_prepare_background(
    const lv_img_dsc_t *clear_wallpaper)
{
    Service_StatusTypeDef status;
    lv_img_dsc_t *blurred_wallpaper;

    if (ui_Boot == NULL)
    {
        return SERVICE_NOT_READY;
    }

    if (!service_gui_theme_uses_wallpaper())
    {
        return SERVICE_OK;
    }

    status = service_gui_canvas_blur_image(
        clear_wallpaper,
        SERVICE_GUI_BOOT_WALLPAPER_BLUR_RADIUS,
        &blurred_wallpaper);

    if (status != SERVICE_OK)
    {
        return status;
    }

    lv_obj_set_style_bg_img_src(
        ui_Boot,
        blurred_wallpaper,
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_opa(
        ui_Boot,
        LV_OPA_COVER,
        LV_PART_MAIN | LV_STATE_DEFAULT);

    return SERVICE_OK;
}

/**
 * @brief 在前一段 Screen 切换完全收尾后启动 BootReveal 到 Lock 的动画。
 * @param user_data 未使用；保留以符合 lv_async_call() 回调签名。
 * @note  此函数只能由 GUI Task 的 LVGL 异步回调执行。此时 LVGL 已清除前一段
 *        Boot 到 BootReveal 切换的内部状态，因此不会触发嵌套切屏的取消分支。
 */
static void service_gui_boot_load_lock_async(void *user_data)
{
    (void)user_data;

    service_gui_boot_lock_request_pending = false;

    if (service_gui_boot_lock_transition_started ||
        (ui_BootReveal == NULL) ||
        (ui_Lock == NULL))
    {
        return;
    }

    service_gui_boot_lock_transition_started = true;

    /* SquareLine 持有 Screen 对象，不能让 LVGL 自动删除旧 Screen。 */
    lv_scr_load_anim(
        ui_Lock,
        LV_SCR_LOAD_ANIM_FADE_OUT,
        SERVICE_GUI_BOOT_LOCK_FADE_TIME_MS,
        SERVICE_GUI_BOOT_REVEAL_HOLD_TIME_MS,
        false);
}

/**
 * @brief SquareLine 的 BootReveal SCREEN_LOADED 事件回调。
 * @param event SquareLine 传入的 LVGL Screen Loaded 事件。
 * @note  此函数由 GUI/ 生成代码按链接符号调用，但实现及状态均归 GUI Service。
 *        它不直接切屏，只投递一次 lv_async_call()，从而避免在 LVGL 前一段
 *        Screen 切换尚未清理内部状态时重入 lv_scr_load_anim()。
 */
void Service_GUI_Boot_RequestLock(lv_event_t *event)
{
    if ((event == NULL) ||
        (lv_event_get_code(event) != LV_EVENT_SCREEN_LOADED) ||
        (lv_event_get_target(event) != ui_BootReveal) ||
        service_gui_boot_lock_request_pending ||
        service_gui_boot_lock_transition_started)
    {
        return;
    }

    service_gui_boot_lock_request_pending = true;

    if (lv_async_call(service_gui_boot_load_lock_async, NULL) != LV_RES_OK)
    {
        service_gui_boot_lock_request_pending = false;
    }
}

/**
 * @brief 启动 Boot 视觉序列。
 * @note  仅允许由 GUI Task 在 ui_init() 和 Boot 背景资源绑定后调用。
 *        局部 lv_anim_t 描述符在 lv_anim_start() 后会被 LVGL 复制，因此不需要
 *        静态保存。Boot Arc 被删除时，LVGL 会自动删除以该对象为目标的动画。
 */
void service_gui_boot_start(void)
{
    lv_anim_t phase_animation;

    service_gui_boot_lock_request_pending = false;
    service_gui_boot_lock_transition_started = false;

    if (ui_BootOrbitRing == NULL)
    {
        return;
    }

    /*
     * 当前 Service_GUI_Init() 只允许进入一次；这里仍先按 var + exec callback
     * 删除同类动画，确保以后支持重新进入 Boot 时不会叠加重复动画。
     */
    (void)lv_anim_del(
        ui_BootOrbitRing,
        service_gui_boot_set_arc_phase);

    /* MAIN 保持完整轨道，统一相位负责 INDICATOR 的转动和伸缩。 */
    lv_arc_set_bg_angles(ui_BootOrbitRing, 0U, 360U);
    lv_arc_set_rotation(ui_BootOrbitRing, 0U);
    service_gui_boot_set_arc_phase(ui_BootOrbitRing, 0);

    lv_anim_init(&phase_animation);
    lv_anim_set_var(&phase_animation, ui_BootOrbitRing);
    lv_anim_set_exec_cb(
        &phase_animation,
        service_gui_boot_set_arc_phase);
    lv_anim_set_values(
        &phase_animation,
        0,
        SERVICE_GUI_BOOT_ARC_PHASE_MAX);
    lv_anim_set_time(
        &phase_animation,
        SERVICE_GUI_BOOT_ARC_CYCLE_TIME_MS);
    lv_anim_set_path_cb(&phase_animation, lv_anim_path_linear);
    lv_anim_set_repeat_count(
        &phase_animation,
        LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&phase_animation);
}
