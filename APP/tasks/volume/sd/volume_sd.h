/**
 ******************************************************************************
 * @file    volume_sd.h
 * @brief   卷任务的 SD 部分：启动、消抖、关闸、对账、卸载与挂载、发布卷事件。
 *
 * @details
 *          卷任务（`volume_task.c`）只留“等通知带超时 → 调一次 `volume_sd_step()`”的循环，所有判断和动作都在这里，
 *          这样每一条路径都能在主机上用假 Filesystem Service 跑到（Tests/volume）。
 *
 *          收尾顺序（#195 第 7 条，卷任务不等订阅者）：
 *          1. 检测边沿：关闸（`PauseSD`），记第一个边沿的时刻。
 *          2. 静默 30 ms，其间每个新边沿重新计时（不另设上限，由播放侧 T_data 兜底）。
 *          3. `RefreshSD`：在 SD 卷锁内刷新，**拿锁就是在等在途调用退出**（真拔卡时它 +1 ms 左右失败返回）。
 *          4. 按 `volume_reconcile()` 的路径做事：卸载（令牌全部失效、槽释放）→ 发卷消失 → 挂载 → 发卷就绪。
 *             挂载失败不发卷就绪，只记日志。
 *          5. 开闸（`ResumeSD`）。**每一次 `PauseSD` 都在这个函数里配一次 `ResumeSD`**：唯一不开闸的情形是
 *             对账期间又来了新边沿，此时闸保持关闭、重新消抖，最终必然再走到第 5 步。
 *          Service 的闸没有超时放行，也没有看门狗（Filesystem README“等闸有没有上界，谁保证开闸”），
 *          所以“`volume_sd_step()` 返回 `portMAX_DELAY` 时闸一定是开的”是这个模块的不变量（主机测试逐路径核对）。
 ******************************************************************************
 */

#ifndef VOLUME_SD_H
#define VOLUME_SD_H

#include <stdbool.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

void volume_sd_detect_callback(void *context);
bool volume_sd_start(TaskHandle_t task);
TickType_t volume_sd_step(TickType_t now, bool edge);

#endif /* VOLUME_SD_H */
