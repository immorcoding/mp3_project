/**
 ******************************************************************************
 * @file    background_task.c
 * @brief   后台任务：静态创建（栈在 AXI SRAM 新段）、订阅卷事件，入口 `for(;;) background_loop_step()`。
 *
 * @details
 *          判断都在 `background_loop.c`（主机可测），本文件只留创建、订阅和循环（`for(;;)` 没法测）。
 *          入口先拼作业表（`background_jobs_build()`：内置的回收作业、默认关闭的压力钩子，加注册表各应用的 `background_step`，
 *          现在是音乐的曲库扫描），再登记卷事件订阅：SD 卷就绪或消失时叫醒扫描，Flash 卷就绪时叫醒回收；
 *          登记后第一圈不等通知（`background_loop_step` 先调一遍作业），所以晚于第一个卷事件启动也不会漏，各作业自己比对卷序号。
 *          静态创建：TCB 在 DTCM `.bss`，6 KiB 栈（1536 word）放 AXI SRAM 新段 `.app_axi_ram`（NOLOAD，只经 CPU 读写，
 *          不作 DMA）。HWD-2：栈在 `xTaskCreateStatic` 里被内核整块填成已知值，首次使用前因此完整写入。
 *          与 ARC-17 字面不符的是 Log、Monitor、GUI 仍为动态栈，不是本任务。
 *
 *          优先级 1，与 Flash 启动任务同级（#220：Flash 启动任务由 2 降到 1，开机前约 8 s 不再抢走扫描的 CPU）、低于前台、GUI、
 *          Log、Monitor（2）：作业只拿空隙，不抢前台。GUI 每圈睡眠（#205），它和前台、Log、Monitor 都睡着时后台任务才轮得到；
 *          GUI 持续刷新的唱盘页上后台任务只分得 7 至 13 % 的 CPU（#191 实测的空闲比例，当时与 GUI 同级；GUI 升到 2 之后份额
 *          不会更大，第 6 次上板重新量），作业慢是预期，不设饥饿兜底（ARC-15）。Flash 启动任务在 `MountFlash` 期间持
 *          Flash 卷锁；后台作业在 Flash 卷就绪之前不碰 Flash 卷，所以不会和它争锁。
 ******************************************************************************
 */

#include "APP/tasks/background/background_task.h"
#include "APP/tasks/background/background_jobs.h"
#include "APP/tasks/background/background_loop.h"
#include "APP/tasks/background/background_task_config.h"
#include "APP/apps/app_table.h"
#include "APP/tasks/volume/volume_event.h"
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
_Static_assert((unsigned)BACKGROUND_NOTIFY_COUNT <= (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES,
               "Background Task notify slots exceed FreeRTOS array length");

/* TCB 静态持有，位于 DTCM .bss，不占内核堆。 */
static StaticTask_t background_task_tcb;
static StackType_t background_task_stack[APP_BACKGROUND_TASK_STACK_WORDS]
    __attribute__((section(".app_axi_ram"), aligned(8)));

/**
 * @brief  后台任务入口：记下自己的句柄、拼作业表、登记卷事件订阅、投钩子的启动日志，然后每圈调一遍作业并等通知。
 * @param[in] handle 未使用，保留以满足 FreeRTOS TaskFunction_t。
 * @note   卷事件的订阅者现在是音频读取、前台、后台三个（`VOLUME_SUBSCRIBER_MAX` 为 4，余一个）。作业表放在任务栈上（`BACKGROUND_JOB_MAX` 项，
 *         32 B）。没有作业要做时 `background_loop_step()` 阻塞在通知上，不占 CPU。
 */
void background_task(void *handle)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    background_job_step_t jobs[BACKGROUND_JOB_MAX];
    const size_t job_count = background_jobs_build(app_table, app_table_count, jobs, BACKGROUND_JOB_MAX);

    (void)handle;
    background_loop_bind(task);
    if (!volume_subscribe(task, (UBaseType_t)BACKGROUND_NOTIFY_EVENT, BACKGROUND_NOTIFY_FLAG_VOLUME))
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               "BACKGROUND",
                               "Volume event subscription failed.");
    }
    background_jobs_announce();

    for (;;)
    {
        (void)background_loop_step(jobs, job_count);
    }
}

/**
 * @brief  静态创建后台任务。
 * @return 任务句柄；创建失败为 NULL（静态创建只在参数为空时失败，实际不会发生）。
 * @note   由 Boot 任务（`app_tasks.c`）调用。任务优先级 1 高于 Boot（0），创建后立即运行到首次阻塞。
 */
TaskHandle_t background_task_create(void)
{
    return xTaskCreateStatic(background_task,
                             "Background Task",
                             APP_BACKGROUND_TASK_STACK_WORDS,
                             NULL,
                             APP_BACKGROUND_TASK_PRIORITY,
                             background_task_stack,
                             &background_task_tcb);
}
