/**
  ******************************************************************************
  * @file    gui_service_main.c
  * @brief   Main Screen 运行时 Module 编排入口。
  *
  * @details
  *          本 Module 只定义 Main Screen 运行时准备的稳定顺序：先由 Pager 完成
  *          布局、初始 Music 页面定位和循环分页事件绑定，再由 Background 根据已
  *          稳定的对象坐标构建局部毛玻璃。具体分页状态和图像资源均由各自子 Module
  *          私有持有；本文件不直接操作 SquareLine 对象或 Canvas 缓冲。
  ******************************************************************************
  */

#include "Service/gui/main/gui_service_main.h"

#include "Service/gui/main/background/gui_service_main_background.h"
#include "Service/gui/main/pager/gui_service_main_pager.h"

/**
 * @brief 准备 Main Screen 的运行时分页与局部毛玻璃效果。
 * @param clear_wallpaper 当前清晰系统壁纸。
 * @retval SERVICE_OK Main Pager 与 Background Module 均已完成初始化。
 * @retval SERVICE_NOT_READY SquareLine Main 对象、布局或内部 Tabview 尚未就绪。
 * @retval SERVICE_INVALID_PARAM 壁纸图片、局部裁剪区域或 Canvas 参数不满足约束。
 * @note 必须在 `ui_init()` 后、Boot 背景准备前由 GUI Service 调用一次。先初始化
 *       Pager 可保证 Background 的首帧裁剪以居中的 MusicPage 实际坐标为准；调用者
 *       不需要也不得直接调用任何 Main 子 Module。
 */
Service_StatusTypeDef service_gui_main_prepare(
    const lv_img_dsc_t *clear_wallpaper)
{
    Service_StatusTypeDef status;

    status = service_gui_main_pager_prepare();

    if (status != SERVICE_OK)
    {
        return status;
    }

    return service_gui_main_background_prepare(clear_wallpaper);
}
