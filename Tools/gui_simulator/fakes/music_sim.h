/**
  ******************************************************************************
  * @file    music_sim.h
  * @brief   假音乐前台流程的模拟器控制入口：切换挂载、模拟重扫（含空库）、曲名与窗口应答的脚本开关（某首名字取不到、
  *          应答延迟若干圈），并给假播放后端查询曲库代次与曲目数。
  *
  * @details
  *          假后端只替换音乐前台流程：`ui_cell`、格子定义和 GUI 音乐分区都编译真实代码。注册表里 `foreground_step` 与 `boot_init`
  *          用到的 `music_foreground_step()`、`music_foreground_boot_init()` 由 music_sim.c 按生产头实现，两个叫醒函数也是替身。
  *          本头只给 sim_main.c 与假播放后端用，不进入生产代码。
  ******************************************************************************
  */

#ifndef MUSIC_SIM_H
#define MUSIC_SIM_H

#include <stdbool.h>
#include <stdint.h>

void music_sim_init(void);
bool music_sim_toggle_mount(void);
void music_sim_rescan(void);
void music_sim_toggle_empty(void);
void music_sim_toggle_name_fail(uint16_t index);
void music_sim_set_name_delay(uint32_t loops);
void music_sim_set_window_delay(uint32_t loops);
bool music_sim_mounted(void);
uint32_t music_sim_generation_get(void);
uint16_t music_sim_track_count(void);

#endif /* MUSIC_SIM_H */
