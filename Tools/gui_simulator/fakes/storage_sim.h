/**
  ******************************************************************************
  * @file    storage_sim.h
  * @brief   假 Storage 的模拟器控制入口：推进窗口、切换挂载与模拟重扫。
  *
  * @details
  *          音乐分区看到的 `storage_listbuffer_*`、`storage_task_sd_is_ready/mounted()`
  *          由 storage_sim.c 按生产头实现；本头只给 sim_main.c 驱动替身用，
  *          不进入生产代码。
  ******************************************************************************
  */

#ifndef STORAGE_SIM_H
#define STORAGE_SIM_H

/**
 * @brief 复位为已挂载、第 1 代、10 首固定曲目，并按新表初始化播放游标。
 * @note  须在 gui_music_init() 之前调用。
 */
void storage_sim_init(void);

/**
 * @brief 代替 Storage Task 走一圈：上一圈留下的 PENDING 窗口在本圈填成 READY。
 * @note  每圈在 gui_music_step() 之前调用，因此 request 恰好在下一圈可 APPLY。
 */
void storage_sim_step(void);

/**
 * @brief C 键：在拔卡与重新挂载之间切换，并把代次 +1。
 * @note  拔卡时作废游标、把 PENDING 打成空窗 READY；挂载时按新代次初始化游标。
 */
void storage_sim_toggle_mount(void);

/**
 * @brief G 键：模拟重扫，代次 +1 并按新代次初始化游标。
 * @note  已显示的 Queue 窗口不主动作废，保留到音乐分区下一次 request，用于制造过期窗口。
 *        未挂载时忽略。
 */
void storage_sim_rescan(void);

#endif /* STORAGE_SIM_H */
