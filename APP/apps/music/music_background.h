/**
 * @file music_background.h
 * @brief 音乐应用的后台作业入口：注册表的 `background_step`。
 */

#ifndef MUSIC_BACKGROUND_H
#define MUSIC_BACKGROUND_H

#include <stdint.h>

/**
 * 后台任务每圈调用一次（注册表的 background_step）：做一小步，返回多少毫秒后再叫它：0 马上，`BACKGROUND_WAIT_FOREVER` 等通知。
 * 现在音乐只有一个后台作业，曲库扫描；以后音乐有第二个后台作业时在这个函数里合并（ARC-22、#197）。
 */
uint32_t music_background_step(void);

#endif /* MUSIC_BACKGROUND_H */
