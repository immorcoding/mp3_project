/**
 ******************************************************************************
 * @file    volume_reconcile.h
 * @brief   卷任务的判断逻辑（纯 C）：消抖后的对账表和静默时间的计算。
 *
 * @details
 *          照 `playback_machine.c` 的先例，判断放在不依赖任务与 Service 的文件里，`volume_task.c` 只留循环和等待。
 *          对账不靠设备状态变化事件，只看“卷现在挂没挂”和“卡的现状”，这样被吞的拔出也能接住（#191 的
 *          `via=presence`，#195 第 2 条）：
 *
 *          | 卷 | 卡 | 路径 |
 *          |---|---|---|
 *          | 挂着 | 不在，或设备没就绪 | `UNMOUNT` |
 *          | 挂着 | 在，但这次刷新重新初始化过（换卡或被吞的拔出） | `REMOUNT` |
 *          | 挂着 | 在，没变化（假抖动） | `GATE_ONLY` |
 *          | 没挂 | 在且就绪 | `MOUNT` |
 *          | 没挂 | 不在 | `IDLE` |
 *
 *          刷新失败时结构里的卡状态不可信（失败也尽力填写，但不能据此挂载）：卷挂着一律卸载（“刷新失败但卡不在
 *          也卸载”的推广：失败而卡在，说明设备已经进了 ERROR，旧卷同样不可用），卷没挂一律无动作。
 ******************************************************************************
 */

#ifndef VOLUME_RECONCILE_H
#define VOLUME_RECONCILE_H

#include <stdbool.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"

/** @brief 一次对账的输入：卷状态加刷新之后卡的现状。 */
typedef struct
{
    bool Mounted;        /**< 刷新之前卷是否挂着（卷事件字里的挂载位）。 */
    bool RefreshOk;      /**< `Service_Filesystem_RefreshSD` 是否返回成功。 */
    bool Present;        /**< 卡检测脚表示槽里有卡。 */
    bool Ready;          /**< 设备 READY。 */
    bool Reinitialized;  /**< 这次刷新把卡（重新）初始化过。 */
} Volume_ReconcileInputTypeDef;

/** @brief 对账结果，也是日志里 `path=` 的取值。 */
typedef enum
{
    VOLUME_PATH_IDLE = 0U,  /**< 没挂，卡不在（或不可用）：无动作。 */
    VOLUME_PATH_GATE_ONLY,  /**< 挂着，卡在，没变化（假抖动）：只开闸。 */
    VOLUME_PATH_MOUNT,      /**< 没挂，卡在且就绪：挂载，成功才发卷就绪。 */
    VOLUME_PATH_UNMOUNT,    /**< 挂着，卡不在或设备没就绪（含刷新失败）：卸载，发卷消失。 */
    VOLUME_PATH_REMOUNT     /**< 挂着，卡在，重新初始化过：卸载、发卷消失、挂载，成功才发卷就绪。 */
} Volume_PathTypeDef;

Volume_PathTypeDef volume_reconcile(const Volume_ReconcileInputTypeDef *input);
bool volume_path_unmounts(Volume_PathTypeDef path);
bool volume_path_mounts(Volume_PathTypeDef path);
const char *volume_path_name(Volume_PathTypeDef path);
TickType_t volume_debounce_remaining(TickType_t now, TickType_t last_edge, TickType_t quiet);

#endif /* VOLUME_RECONCILE_H */
