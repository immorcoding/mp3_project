/**
 ******************************************************************************
 * @file    volume_task.c
 * @brief   卷任务入口：创建 Flash 启动任务、启动 SD，然后循环“等通知带超时 → 调一次 volume_sd_step()”。
 *
 * @details
 *          照 `playback_machine.c` 的先例，判断和动作都在可以主机测试的文件里（`volume_sd.c`、
 *          `volume_reconcile.c`、`volume_event.c`），本文件只留创建、循环和等待（`for(;;)` 没法测）。
 *          消抖的定时不用软件定时器：等通知带超时，超时就是静默够了；新边沿重新计时。
 *          卡检测回调运行在 ISR，只发任务通知（`volume_sd_detect_callback`）。
 ******************************************************************************
 */

#include "APP/tasks/volume/volume_task.h"
#include "APP/tasks/volume/flash/volume_flash.h"
#include "APP/tasks/volume/sd/volume_sd.h"

#include <stdint.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include "Service/log/log_service.h"

/**
 * @brief  运行卷任务：先创建 Flash 启动任务（不等它），再启动 SD，然后进入消抖循环。
 * @param[in] handle 未使用，保留以满足 FreeRTOS TaskFunction_t。
 * @note   启动顺序（#195 第 12 条）：入口先创建 Flash 启动任务，再初始化 SD。卡在就挂载并发卷就绪，
 *         无卡不发事件，序号保持 0；挂载失败不发事件，只记日志，SD 等下一次边沿再试。SD 基准（默认关闭）
 *         在发卷就绪之前跑。Flash 启动任务优先级低于本任务，`MountFlash` 3.3 至 8.3 s 期间本任务照常处理插拔卡。
 *
 *         循环里第一圈不等（等待 0 tick）：启动期间到达的边沿还留在通知槽里，不会丢。以后每圈等到通知（边沿）
 *         或超时（静默够了，或假抖动钩子到期），再调一次 `volume_sd_step()`，它返回下一圈最多等多久。
 */
void volume_task(void *handle)
{
    TaskHandle_t task_handle;
    TickType_t wait_ticks = 0U;

    (void)handle;
    task_handle = xTaskGetCurrentTaskHandle();

    _Static_assert((unsigned)VOLUME_NOTIFY_COUNT <= (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES,
                   "Volume Task notify slots exceed FreeRTOS array length");

    if (volume_flash_task_create() == NULL)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, "VOLUME", "Flash boot task create failed.");
    }

    (void)volume_sd_start(task_handle);

    for (;;)
    {
        uint32_t flags = 0U;

        (void)xTaskNotifyWaitIndexed((UBaseType_t)VOLUME_NOTIFY_EVENT,
                                     0U,
                                     VOLUME_NOTIFY_FLAG_EVENT_MASK,
                                     &flags,
                                     wait_ticks);
        wait_ticks = volume_sd_step(xTaskGetTickCount(), (flags & VOLUME_NOTIFY_FLAG_SD_DETECT) != 0U);
    }
}
