/**
  ******************************************************************************
  * @file    playback_sim.c
  * @brief   模拟器假播放后端的占位实现：只为让真实 gui_music 链接通过。
  * @note    #84 起改为实现 #71 合同的完整假后端（命令 FIFO、虚拟时钟、挂起与拔卡）。
  ******************************************************************************
  */

#include "APP/tasks/playback/playback_contract.h"

#include <string.h>

/**
 * @brief 占位：接受任何命令，不做处理。
 * @param[in] command 命令；占位实现不读取。
 * @return 始终为 PLAYBACK_OK。
 */
Playback_StatusTypeDef playback_command_send(const Playback_CommandTypeDef *command)
{
    (void)command;
    return PLAYBACK_OK;
}

/**
 * @brief 占位：返回全零快照。
 * @param[out] snapshot 快照。
 */
void playback_snapshot_read(Playback_SnapshotTypeDef *snapshot)
{
    if (snapshot != NULL)
    {
        (void)memset(snapshot, 0, sizeof(*snapshot));
    }
}
