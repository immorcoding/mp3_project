/**
  ******************************************************************************
  * @file    gui_service_main_pager_config.h
  * @brief   Main Screen 循环分页的私有参数。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_MAIN_PAGER_CONFIG_H
#define GUI_SERVICE_MAIN_PAGER_CONFIG_H

/* gui_service_main_pager.c */
#define SERVICE_GUI_MAIN_PAGER_SWITCH_THRESHOLD_PERCENT  (50U)   /* 松手翻至相邻物理槽所需的最小拖动距离占视口宽度比例；同一手势最多一页。 */
#define SERVICE_GUI_MAIN_PAGER_DOT_ANIMATION_TIME_MS     (160U)  /* Main 底部分页指示器伸缩与透明度动画时长，单位为毫秒。 */

#endif /* GUI_SERVICE_MAIN_PAGER_CONFIG_H */
