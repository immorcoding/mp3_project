/**
  ******************************************************************************
  * @file    gui_service_config.h
  * @brief   GUI Service 的私有固定资源配置。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_CONFIG_H
#define GUI_SERVICE_CONFIG_H

/* gui_service.c */
#define SERVICE_GUI_DRAW_BUFFER_LINES      (320U)   /* 单个 LVGL 绘制缓冲包含的屏幕行数。当前为完整 320 行，两块 RGB565 缓冲均位于 SDRAM。 */

/* main/transport/gui_service_main_transport.c */
#define SERVICE_GUI_MUSIC_SKIP_NOTICE_MS   (2000U)  /* 状态行 `Skipped k file(s)` 提示的保持时长；期间再跳过则累加并重新计时（GUI-19）。按 LVGL tick 计时。 */
#define SERVICE_GUI_MUSIC_STATUS_FLASH_MS  (300U)   /* 状态行闪一次（text_opa 淡出再淡回）的总时长（GUI-19）。按 LVGL tick 计时。 */

/* view/gui_service_view_music.c、main/title/gui_service_main_title.c */
#define SERVICE_GUI_MUSIC_TITLE_WIDTH      (192)    /* Now Playing 曲名与歌手可见窗口宽度，像素；曲名更宽时循环滚动，歌手更宽时截断加省略号（GUI-26）。 */
#define SERVICE_GUI_MUSIC_TITLE_SPEED      (24)     /* 曲名循环滚动速度，像素每秒。 */
#define SERVICE_GUI_MUSIC_ARTIST_OPA       (180)    /* 歌手文字 Opa（Ink），比曲名弱一级；与 Queue 歌手同值。 */

/* view/gui_service_view_music.c：Now Playing 纵向四组排布（GUI-27）。各元素 Y、唱盘直径与组间距 S 由下列值与 Playing 内容区高度推出。 */
#define SERVICE_GUI_MUSIC_LAYOUT_GRID          (4)    /* 间距网格，像素；S 按它递减。 */
#define SERVICE_GUI_MUSIC_LAYOUT_GAP_MAX       (12)   /* 组间距 S 的首选上限：标签栏→唱盘、唱盘→曲名、时间→三键、三键→页底。 */
#define SERVICE_GUI_MUSIC_LAYOUT_GAP_MIN       (4)    /* 组间距 S 的下限。 */
#define SERVICE_GUI_MUSIC_LAYOUT_DISC_MIN      (112)  /* 唱盘可见直径下限；推出的直径不足时先缩小 S。 */
#define SERVICE_GUI_MUSIC_LAYOUT_TEXT_GAP      (4)    /* 曲名字形下沿 → 歌手字形上沿。 */
#define SERVICE_GUI_MUSIC_LAYOUT_TEXT_PROGRESS (4)    /* 歌手字形下沿 → 进度条上沿；独立于 S，曲名、歌手、进度条读作一组。 */
#define SERVICE_GUI_MUSIC_LAYOUT_PROGRESS_GAP  (4)    /* 进度条下沿 → 时间字形上沿。 */
#define SERVICE_GUI_MUSIC_LAYOUT_SLIDER_HEIGHT (4)    /* 进度条高（含 1 px 边）；取 4 使唱盘直径为偶数，奇数余量加给进度条。 */
#define SERVICE_GUI_MUSIC_LAYOUT_COLUMN_WIDTH  (192)  /* 横向内容列宽：进度条两端与两侧键外沿；曲名、歌手窗口在列内居中。 */
#define SERVICE_GUI_MUSIC_LAYOUT_SIDE_BUTTON   (36)   /* 上一首、下一首直径；外沿对齐内容列两端。 */
#define SERVICE_GUI_MUSIC_LAYOUT_MAIN_BUTTON   (44)   /* 播放/暂停直径；三键共用水平中线。 */
#define SERVICE_GUI_MUSIC_LAYOUT_TEXT_INK_REF  "Hdg"  /* 曲名、歌手完整字形的参考字符（大写、上伸部、下伸部），取最高上沿与最低下沿；Noto 12 推出 Label 内第 3–15 行（d 比 H 高 1 行）。 */
#define SERVICE_GUI_MUSIC_LAYOUT_TIME_INK_REF  "0123456789:/-" /* 时间文本可能出现的全部字符；Montserrat 10 推出 Label 内第 1–9 行。 */

/* view/gui_service_view_music.c、main/transport/gui_service_main_transport.c */
#define SERVICE_GUI_MUSIC_PLAY_ICON_SHIFT_X    (1)    /* 播放三角形质心偏左约 1.5 px，右移 1 px 光学居中；暂停与两侧键图标不移。 */

#endif /* GUI_SERVICE_CONFIG_H */
