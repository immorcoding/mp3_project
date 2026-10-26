/**
  ******************************************************************************
  * @file    storage_sim.h
  * @brief   假 Storage 的模拟器控制入口：推进窗口与名称槽、切换挂载、模拟重扫（含空库）与查询曲库。
  *
  * @details
  *          音乐分区看到的 `storage_listbuffer_*`、`storage_namebuffer_*`、`storage_task_sd_is_ready/mounted()`
  *          由 storage_sim.c 按生产头实现；本头只给 sim_main.c 与假播放后端用，
  *          不进入生产代码。
  ******************************************************************************
  */

#ifndef STORAGE_SIM_H
#define STORAGE_SIM_H

#include <stdbool.h>
#include <stdint.h>

void storage_sim_init(void);
void storage_sim_step(void);
bool storage_sim_toggle_mount(void);
void storage_sim_rescan(void);
void storage_sim_toggle_empty(void);
uint32_t storage_sim_generation_get(void);
uint16_t storage_sim_track_count(void);

#endif /* STORAGE_SIM_H */
