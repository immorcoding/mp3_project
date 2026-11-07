/**
 ******************************************************************************
 * @file    foreground_task.c
 * @brief   前台任务：静态创建（栈在 AXI SRAM 新段）与入口 `for(;;) foreground_loop_step()`。
 *
 * @details
 *          判断都在 `foreground_loop.c`（主机可测），本文件只留创建和循环（`for(;;)` 没法测）。
 *          每圈顺序：轮流调用各应用的 `foreground_step`（量耗时）→ `ui_cell` 持锁统计与 step 最长值的汇总行 → 按最小返回值等通知。
 *          叫醒它的有三种：GUI 写了需求（`ui_cell` 的叫醒回调）、曲库发布（`catalog.c` 调 `foreground_task_wake()`）、
 *          卷事件（入口登记订阅）。FLAG 只用于唤醒，醒来后各应用自己比对卷序号、重看最新需求。
 *
 *          静态创建：TCB 在 DTCM `.bss`，6 KiB 栈（1536 word）放 AXI SRAM 新段 `.app_axi_ram`（NOLOAD，只经 CPU 读写，
 *          不作 DMA）。HWD-2：栈在 `xTaskCreateStatic` 里被内核整块填成已知值，首次使用前因此完整写入。
 *          优先级 2，与 GUI、Log、Monitor、Flash 启动任务同级（时间片轮转），低于卷、音频读取（3）与播放（4），高于 Storage 与
 *          后台（1）。前台任务不在 Filesystem 的拒绝名单里：它可以调文件接口（GUI 和播放不行）。
 ******************************************************************************
 */

#include "APP/tasks/foreground/foreground_task.h"
#include "APP/tasks/foreground/foreground_loop.h"
#include "APP/tasks/foreground/foreground_task_config.h"
#include "APP/tasks/volume/volume_event.h"
#include "APP/ui_cell/ui_cell.h"
#include "APP/apps/app_table.h"
#include "APP/app_config.h"

#include <stddef.h>

#include "Service/log/log_service.h"

/*
 * HWD-2：.app_axi_ram 是 NOLOAD，不自动清零。静态栈在 xTaskCreateStatic 里被整块填成已知值
 * （tskSET_NEW_STACKS_TO_KNOWN_VALUE，由下面两个配置之一打开），首次使用前因此完整写入。
 * 配置变化让这个前提失效时，在编译期报错，而不是留下一块没写过的栈。
 */
_Static_assert((configCHECK_FOR_STACK_OVERFLOW > 1) || (configUSE_TRACE_FACILITY == 1),
               "static task stacks in NOLOAD .app_axi_ram rely on the kernel filling them at creation (HWD-2)");
_Static_assert((unsigned)FOREGROUND_NOTIFY_COUNT <= (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES,
               "Foreground Task notify slots exceed FreeRTOS array length");
_Static_assert(sizeof(APP_FOREGROUND_TASK_NAME) <= (size_t)configMAX_TASK_NAME_LEN,
               "the task name must fit configMAX_TASK_NAME_LEN or the Monitor registration will not match");

/* TCB 静态持有，位于 DTCM .bss，不占内核堆。 */
static StaticTask_t foreground_task_tcb;
static StackType_t foreground_task_stack[APP_FOREGROUND_TASK_STACK_WORDS]
    __attribute__((section(".app_axi_ram"), aligned(8)));

/**
 * @brief  每圈在等待之前调一次：`ui_cell` 持锁统计的汇总行（Debug 才有，到点且有新数据才投）。
 * @note   在任务里投日志，不在 GUI 帧路径上投（DIAG-3）。
 */
static void foreground_task_after_round(void)
{
    ui_cell_stats_poll((uint32_t)xTaskGetTickCount());
    foreground_loop_report((uint32_t)xTaskGetTickCount());
}

/**
 * @brief  前台任务入口：记下自己的句柄、登记卷事件订阅，然后每圈调一遍各应用的 step 并等通知。
 * @param[in] handle 未使用，保留以满足 FreeRTOS TaskFunction_t。
 * @note   登记订阅后第一圈不等通知（`foreground_loop_step` 先调一遍 step）：任务晚于第一条需求或第一个卷事件启动也不会漏，
 *         各应用的 step 自己比对卷序号、重看最新需求。
 */
void foreground_task(void *handle)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();

    (void)handle;
    foreground_loop_reset();
    foreground_loop_bind(task);

    if (!volume_subscribe(task, (UBaseType_t)FOREGROUND_NOTIFY_EVENT, FOREGROUND_NOTIFY_FLAG_VOLUME))
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               "FOREGROUND",
                               "Volume event subscription failed.");
    }

    for (;;)
    {
        (void)foreground_loop_step(app_table, app_table_count, foreground_task_after_round);
    }
}

/**
 * @brief  静态创建前台任务。
 * @return 任务句柄；创建失败为 NULL（静态创建只在参数为空时失败，实际不会发生）。
 * @note   由 Boot 任务（`app_tasks.c`）调用，必须晚于 `app_table_boot_init()`：任务优先级 2 高于 Boot（0），创建后立即运行，
 *         第一圈就会调各应用的 step，格子和缓存键表必须已经准备好。
 */
TaskHandle_t foreground_task_create(void)
{
    return xTaskCreateStatic(foreground_task,
                             APP_FOREGROUND_TASK_NAME,
                             APP_FOREGROUND_TASK_STACK_WORDS,
                             NULL,
                             APP_FOREGROUND_TASK_PRIORITY,
                             foreground_task_stack,
                             &foreground_task_tcb);
}
