/**
 ******************************************************************************
 * @file    volume_reconcile.c
 * @brief   卷对账表与消抖静默时间的实现（纯 C，见 volume_reconcile.h）。
 ******************************************************************************
 */

#include "APP/tasks/volume/volume_reconcile.h"

/**
 * @brief  按 #195 的 5 行表决定这次对账走哪条路径。
 * @param[in] input 对账输入，不为空。
 * @return 路径。
 */
Volume_PathTypeDef volume_reconcile(const Volume_ReconcileInputTypeDef *input)
{
    /* 刷新失败时卡状态不可信：不能据此挂载；设备已经进了 ERROR 或卡已不在，旧卷同样不可用。 */
    const bool usable = input->RefreshOk && input->Present && input->Ready;

    if (input->Mounted)
    {
        if (!usable)
        {
            return VOLUME_PATH_UNMOUNT;
        }
        return input->Reinitialized ? VOLUME_PATH_REMOUNT : VOLUME_PATH_GATE_ONLY;
    }

    return usable ? VOLUME_PATH_MOUNT : VOLUME_PATH_IDLE;
}

/**
 * @brief  这条路径是否要先卸载。
 * @param[in] path 路径。
 * @return 要卸载（并发卷消失）为真。
 */
bool volume_path_unmounts(Volume_PathTypeDef path)
{
    return (path == VOLUME_PATH_UNMOUNT) || (path == VOLUME_PATH_REMOUNT);
}

/**
 * @brief  这条路径是否要尝试挂载。
 * @param[in] path 路径。
 * @return 要挂载（成功才发卷就绪）为真。
 */
bool volume_path_mounts(Volume_PathTypeDef path)
{
    return (path == VOLUME_PATH_MOUNT) || (path == VOLUME_PATH_REMOUNT);
}

/**
 * @brief  路径在日志里的名字。
 * @param[in] path 路径。
 * @return 静态字符串。
 */
const char *volume_path_name(Volume_PathTypeDef path)
{
    switch (path)
    {
        case VOLUME_PATH_GATE_ONLY:
            return "gate_only";
        case VOLUME_PATH_MOUNT:
            return "mount";
        case VOLUME_PATH_UNMOUNT:
            return "unmount";
        case VOLUME_PATH_REMOUNT:
            return "remount";
        case VOLUME_PATH_IDLE:
        default:
            return "idle";
    }
}

/**
 * @brief  距静默时间结束还剩多少 tick。
 * @param[in] now 当前 tick。
 * @param[in] last_edge 最后一个边沿的 tick。
 * @param[in] quiet 静默时间。
 * @return 剩余 tick，已到点为 0。
 */
TickType_t volume_debounce_remaining(TickType_t now, TickType_t last_edge, TickType_t quiet)
{
    const TickType_t elapsed = now - last_edge; /* 无符号减法，tick 回绕也对。 */

    return (elapsed >= quiet) ? 0U : (quiet - elapsed);
}
