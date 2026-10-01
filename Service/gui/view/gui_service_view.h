/**
  ******************************************************************************
  * @file    gui_service_view.h
  * @brief   GUI Service 私有界面层：创建全部 Screen 并发布对象句柄。
  *
  * @details
  *          本 Module 只负责构建对象树与静态样式；交互、动画与离屏效果由
  *          boot/、main/、theme/ 等行为 Module 通过本头文件的句柄访问对象。
  *          APP、其他 Service 不得包含本头文件。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_VIEW_H
#define GUI_SERVICE_VIEW_H

#include "Service/service.h"
#include "lvgl.h"

/** @brief Boot Screen 句柄；Boot 切走后被删除，句柄回到 NULL。 */
typedef struct
{
    lv_obj_t *screen;      /**< Boot Screen 根对象。 */
    lv_obj_t *orbit_ring;  /**< 启动环 Arc。 */
} Service_GUI_ViewBootTypeDef;

/** @brief Main Screen 外壳句柄。 */
typedef struct
{
    lv_obj_t *screen;          /**< Main Screen 根对象。 */
    lv_obj_t *page_container;  /**< 横向循环分页视口。 */
    lv_obj_t *settings_page;   /**< 设置页，初始位于左槽。 */
    lv_obj_t *music_page;      /**< 音乐页，初始位于中槽。 */
    lv_obj_t *books_page;      /**< 阅读页，初始位于右槽。 */
    lv_obj_t *dot_settings;    /**< 设置页分页圆点。 */
    lv_obj_t *dot_music;       /**< 音乐页分页圆点（初始为活动胶囊）。 */
    lv_obj_t *dot_books;       /**< 阅读页分页圆点。 */
} Service_GUI_ViewMainTypeDef;

/** @brief Music 页句柄。 */
typedef struct
{
    lv_obj_t *tabs;               /**< Playing / Queue / Library 标签视图。 */
    lv_obj_t *queue_tab;          /**< Queue 竖向滚动列表容器。 */
    lv_obj_t *vinyl_image;        /**< Now Playing 唱盘 Image。 */
    lv_obj_t *slider;             /**< 播放进度条。 */
    lv_obj_t *previous_button;    /**< 上一首。 */
    lv_obj_t *play_pause_button;  /**< 播放/暂停。 */
    lv_obj_t *play_pause_icon;    /**< 播放/暂停符号 Label。 */
    lv_obj_t *next_button;        /**< 下一首。 */
} Service_GUI_ViewMusicTypeDef;

/** @brief 全部 Screen 的句柄集合。 */
typedef struct
{
    Service_GUI_ViewBootTypeDef boot;
    lv_obj_t *boot_reveal;               /**< 清晰壁纸过渡 Screen。 */
    lv_obj_t *lock;                      /**< 锁屏 Screen。 */
    Service_GUI_ViewMainTypeDef main;
    Service_GUI_ViewMusicTypeDef music;
} Service_GUI_ViewTypeDef;

Service_StatusTypeDef service_gui_view_create(void);
const Service_GUI_ViewTypeDef *service_gui_view_get(void);
const lv_img_dsc_t *service_gui_view_wallpaper(void);

#endif /* GUI_SERVICE_VIEW_H */
