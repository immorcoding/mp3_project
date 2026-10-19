/**
  ******************************************************************************
  * @file    playback_sim.h
  * @brief   假播放后端的模拟器控制入口：推进虚拟时钟、挂起命令处理与拔卡。
  *
  * @details
  *          音乐分区看到的 `playback_command_send()`、`playback_snapshot_read()` 由
  *          playback_sim.c 按 `playback_contract.h` 实现；本头只给 sim_main.c 驱动
  *          替身用，不进入生产代码。
  ******************************************************************************
  */

#ifndef PLAYBACK_SIM_H
#define PLAYBACK_SIM_H

#include <stdint.h>

void playback_sim_init(void);
void playback_sim_step(uint32_t now_ms);
void playback_sim_toggle_suspend(void);
void playback_sim_card_removed(void);

#endif /* PLAYBACK_SIM_H */
