/**
 ******************************************************************************
 * @file    audio_reader_task.c
 * @brief   音频读取任务：静态创建（栈在 AXI SRAM 新段）与入口循环 卷事件 → 请求信箱 → 读一块 → 决定等多久。
 *
 * @details
 *          判断都在可主机测试的纯函数里（`audio_reader_result`、`audio_reader_pace`、`audio_reader_diag`、
 *          `audio_reader_range`），文件调用和信箱在 `audio_reader_stream.c`；本文件只留创建和循环（`for(;;)` 没法测）。
 *
 *          **一个等待点**：叫醒它的只有新请求、块归还、卷事件，都是通知位，取走前一直保留。每圈不管是哪个位叫醒的，
 *          都按固定顺序做：卷事件 → 请求信箱 → 读一块 → 决定等多久。FLAG 只用于唤醒，醒来后以卷序号、信箱和队列为准，
 *          所以合并的叫醒、迟到的叫醒、没带标志的超时都不会丢事实。**进入循环时第一圈不等**（等待 0 tick），
 *          先看一次卷序号和信箱，不依赖通知：任务晚于第一条请求启动也不会漏。
 *          让路用这个等待点（水位以上每读一块等 1 tick），不用 `vTaskDelay`：让路期间新请求、块归还、卷事件能立刻叫醒。
 *
 *          静态创建：TCB 在 DTCM `.bss`，4 KiB 栈（1024 word）放 AXI SRAM 新段 `.app_axi_ram`（NOLOAD，只经 CPU 读写，
 *          不作 DMA）。HWD-2：栈在 `xTaskCreateStatic` 里被内核整块填成已知值，首次使用前因此完整写入。
 *          优先级 3，与卷任务、定时器服务任务同级（时间片轮转），低于播放（4），高于前台、GUI、Log、Monitor（2）与后台、Flash 启动任务（1，#218、#220）。
 ******************************************************************************
 */

#include "APP/tasks/audio_reader/audio_reader_task.h"
#include "APP/tasks/audio_reader/audio_reader_block.h"
#include "APP/tasks/audio_reader/audio_reader_config.h"
#include "APP/tasks/audio_reader/audio_reader_pace.h"
#include "APP/tasks/audio_reader/audio_reader_stream.h"
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
_Static_assert((unsigned)AUDIO_READER_NOTIFY_COUNT <= (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES,
               "Audio Read Task notify slots exceed FreeRTOS array length");
_Static_assert(sizeof(APP_AUDIO_READER_TASK_NAME) <= (size_t)configMAX_TASK_NAME_LEN,
               "the task name must fit configMAX_TASK_NAME_LEN or the Monitor registration will not match");
_Static_assert((unsigned)AUDIO_READER_NOTIFY_EVENT_INDEX < (unsigned)AUDIO_READER_NOTIFY_COUNT,
               "the single wait point must be one of the task's notify slots");

/* TCB 静态持有，位于 DTCM .bss，不占内核堆。 */
static StaticTask_t audio_reader_task_tcb;
static StackType_t audio_reader_task_stack[APP_AUDIO_READER_TASK_STACK_WORDS]
    __attribute__((section(".app_axi_ram"), aligned(8)));

/**
 * @brief  Boot 任务里的内存准备：块池和两个信箱。
 * @note   由 `create_task`（Boot，192 word 栈）在创建音频读取任务和播放任务之前调用一次，晚于可选的 SDRAM 全容量诊断
 *         （诊断是破坏性的，必须独占，块池 NOLOAD 段此后才有主，HWD-2）。栈核算：最深一条是 `audio_reader_block_init()`
 *         → `xQueueSend`，见 `APP/tasks/audio_reader/README.md`“Boot 栈核算”。
 */
void audio_reader_boot_init(void)
{
    audio_reader_block_init();
    audio_reader_stream_boot_init();
}

/**
 * @brief  音频读取任务入口：记下自己的句柄、登记卷事件订阅，然后循环。
 * @param[in] handle 未使用，保留以满足 FreeRTOS TaskFunction_t。
 * @note   每圈顺序：卷事件 → 请求信箱 → 读一块 → 决定等多久；第一圈不等。订阅登记后第一圈就会比对一次卷序号，
 *         所以任务比卷任务晚启动或早启动都不会漏。
 */
void audio_reader_task(void *handle)
{
    TaskHandle_t task_handle;
    uint32_t wait_ticks = 0U; /* 第一圈不等：先看一次卷序号和信箱，不依赖通知。 */

    (void)handle;
    task_handle = xTaskGetCurrentTaskHandle();
    audio_reader_stream_bind(task_handle);

    if (!volume_subscribe(task_handle, AUDIO_READER_NOTIFY_EVENT_INDEX, AUDIO_READER_NOTIFY_FLAG_VOLUME))
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, "Audio", "Volume event subscription failed.");
    }

    if (AUDIO_READER_HOOK_LOCATOR_MISS != 0U)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, "HOOK", "locator_miss ON");
    }

    for (;;)
    {
        uint32_t flags = 0U;
        AudioReader_PaceInputTypeDef pace;

        /* 唯一的等待点。FLAG 只用于唤醒；超时（让路、SERVICE_BUSY 重试时刻）也是一次唤醒。 */
        (void)xTaskNotifyWaitIndexed((UBaseType_t)AUDIO_READER_NOTIFY_EVENT_INDEX,
                                     0U,
                                     AUDIO_READER_NOTIFY_FLAG_EVENT_MASK,
                                     &flags,
                                     (wait_ticks == AUDIO_READER_WAIT_FOREVER) ? portMAX_DELAY : (TickType_t)wait_ticks);

        audio_reader_stream_apply_volume();      /* 1. 卷事件 */
        audio_reader_stream_process_request();   /* 2. 请求信箱 */
        audio_reader_stream_read_block();        /* 3. 读一块 */

        audio_reader_stream_pace_input(&pace);   /* 4. 决定等多久 */
        wait_ticks = audio_reader_wait_ticks(&pace);
    }
}

/**
 * @brief  静态创建音频读取任务。
 * @return 任务句柄；创建失败为 NULL（静态创建只在参数为空时失败，实际不会发生）。
 * @note   由 Boot 任务（`app_tasks.c`）调用，须在 `audio_reader_boot_init()` 之后。任务优先级 3 高于 Boot（0），
 *         创建后立即运行到第一次等待。
 */
TaskHandle_t audio_reader_task_create(void)
{
    return xTaskCreateStatic(audio_reader_task,
                             APP_AUDIO_READER_TASK_NAME,
                             APP_AUDIO_READER_TASK_STACK_WORDS,
                             NULL,
                             APP_AUDIO_READER_TASK_PRIORITY,
                             audio_reader_task_stack,
                             &audio_reader_task_tcb);
}
