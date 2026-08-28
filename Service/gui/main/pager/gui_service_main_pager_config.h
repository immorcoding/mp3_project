/**
  ******************************************************************************
  * @file    gui_service_main_pager_config.h
  * @brief   Main Screen 循环分页的私有参数。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_MAIN_PAGER_CONFIG_H
#define GUI_SERVICE_MAIN_PAGER_CONFIG_H

/**
 * @brief MainPageContainer 松手翻至相邻物理槽位所需的最小拖动距离占视口宽度比例。
 * @note 同一手势最多切换一页。当前不使用速度判定；若日后启用，必须连同真机验证
 *       结果同步到 GUI 设计文档。
 */
#define SERVICE_GUI_MAIN_PAGER_SWITCH_THRESHOLD_PERCENT  (50U)

/** @brief Main 底部分页指示器伸缩与透明度动画时长。 */
#define SERVICE_GUI_MAIN_PAGER_DOT_ANIMATION_TIME_MS  (160U)

#endif /* GUI_SERVICE_MAIN_PAGER_CONFIG_H */
