/**
 * @file music_app.h
 * @brief 音乐应用的 Boot 入口：注册表的 `boot_init`。
 */

#ifndef MUSIC_APP_H
#define MUSIC_APP_H

/* Boot 任务在创建任何用到格子和缓存的任务之前调用一次：初始化四个界面数据格子，清零行缓存键表与前台流程状态。 */
void music_boot_init(void);

#endif /* MUSIC_APP_H */
