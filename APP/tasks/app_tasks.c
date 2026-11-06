/**
  ******************************************************************************
  * @file    app_tasks.c
  * @brief   FreeRTOS 应用任务创建和调度器启动。
  *
  * @details
  *          Create Task（Boot）先建立 Log Service 的静态消息池与 FreeRTOS 队列，再创建
  *          消费日志的 Log Task，做内存准备（可选的 SDRAM 自检），然后创建卷事件的订阅者
  *          Storage Task、负责 SD 热插拔与挂载并发布卷事件的 Volume Task（静态创建）、
  *          持有播放状态机的 Playback Task（静态创建）、监控 Task、独占
  *          LVGL 执行上下文的 GUI Task 以及轮流调后台作业的 Background Task（静态创建）。
  *          所有任务创建成功后 Create Task 自删。
  ******************************************************************************
  */

#include "APP/tasks/app_tasks.h"

#include "APP/apps/music/catalog/catalog.h"
#include "APP/tasks/background/background_task.h"
#include "APP/tasks/background/background_task.h"
#include "APP/tasks/log/log_task.h"
#include "APP/tasks/monitor/monitor_task.h"
#include "APP/tasks/playback/playback_task.h"
#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/volume/volume_task.h"
#include "APP/tasks/gui/gui_task.h"

#if STORAGE_SDRAM_BENCHMARK_ENABLE
#include "APP/tasks/volume/benchmark/storage_sdram_benchmark.h"

/*
 * Boot 任务（本文件的 create_task）只有 APP_BOOT_TASK_STACK_WORDS = 128 word（512 B），装不下自检：
 * 最深的调用链是 create_task → storage_sdram_benchmark_run（局部 128 B 文本加诊断结构）→ newlib-nano 的 snprintf
 * → _svfiprintf_r → _printf_i → _printf_common → __ssputs_r → memcpy，实测栈帧加起来 Debug 648 B、Release 616 B，
 * 再加一次任务切换最坏要在任务栈上留的约 204 B（Cortex-M7 带 FPU：硬件压栈 104 B 加软件保存 100 B），
 * 需要约 820 至 850 B，超出 512 B。核算过程见 APP/tasks/volume/README.md“Boot 栈核算”。
 * 打开自检之前必须先决定怎么办（加大 Boot 栈，或把自检放到别处），所以这里在编译期拦住，而不是留一个只在打开开关时才
 * 溢出的栈。320 word（1280 B）是按上面的数字给出的最小建议值，不是已经决定的值。
 */
_Static_assert(APP_BOOT_TASK_STACK_WORDS >= 320U,
               "SDRAM self-test in the Boot task needs a larger Boot stack: see APP/tasks/volume/README.md (Boot stack budget)");
#endif

#include "Service/filesystem/filesystem_service.h"
#include "Service/log/log_service.h"
#include "generated/fw_version.h"
#include "main.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/* Playback Task 的 TCB 与栈静态持有（A7），位于 DTCM .bss，不占内核堆。 */
static StaticTask_t playback_task_tcb;
static StackType_t playback_task_stack[APP_PLAYBACK_TASK_STACK_WORDS];

/*
 * Volume Task 的 TCB 静态持有，位于 DTCM .bss；栈放 AXI SRAM 新段 .app_axi_ram（NOLOAD，不自动清零，
 * 只经 CPU 读写，不作 DMA）。HWD-2：栈在 xTaskCreateStatic 里被内核整块填成已知值（tskSET_NEW_STACKS_TO_KNOWN_VALUE），
 * 首次使用前因此完整写入；下面的断言在配置让这个前提失效时在编译期报错。
 */
_Static_assert((configCHECK_FOR_STACK_OVERFLOW > 1) || (configUSE_TRACE_FACILITY == 1),
               "static task stacks in NOLOAD .app_axi_ram rely on the kernel filling them at creation (HWD-2)");
static StaticTask_t volume_task_tcb;
static StackType_t volume_task_stack[APP_VOLUME_TASK_STACK_WORDS]
    __attribute__((section(".app_axi_ram"), aligned(8)));

/**
  * @brief  创建应用运行所需的全部任务，然后删除自身。
  * @param  handle 当前未使用，保留为 FreeRTOS TaskFunction_t 规定的参数。
  * @note   启动分三段：内存准备归本任务（可选的 SDRAM 自检，做完才创建用到 SDRAM 的任务）；卷启动由 Volume Task
  *         决定时机（它入口先创建 Flash 启动任务，再初始化 SD）；SD 基准在 Volume Task 发卷就绪之前跑。
  *         曲库锁（`catalog_boot_init()`，静态互斥量）也在内存准备里创建，早于创建 Storage Task，
  *         所以早于任何任务第一次读写曲库。
  *         Service_Log 必须先于任意可能调用 Service_Log_Post() 的任务创建。SDRAM 自检在 Log Task 创建之后、
  *         Storage Task 创建之前运行（独占 SDRAM，块池 NOLOAD 段此后才有主，HWD-2）；本任务只有 128 word 栈，
  *         自检加 snprintf 的栈深度核算见 `APP/tasks/volume/README.md`。
  *         Storage Task 优先级高于本任务（Boot）：创建它时它先运行到首次阻塞，已完成 storage_audio_stream_init()
  *         并登记卷事件订阅，播放任务才能调用 Storage 请求信箱（Boot 优先级最低，任何更高优先级的任务没阻塞时
  *         本任务不会继续）。Volume Task 在 Storage Task 之后创建：它发布的卷事件有订阅者，
  *         即使晚于它订阅也不会漏（登记后先比对一次序号）。
  *         Playback Task 优先级 4 高于 Create Task，创建后立即运行到首次阻塞，
  *         完成命令 FIFO、状态信箱与回调注册，早于 GUI Task 发出第一条命令。
  *         它以 xTaskCreateStatic 创建，句柄只用于登记拒绝名单（任务经自身句柄接收通知）。
  *         GUI Task 持续推进 LVGL；SPI DMA 刷新时会由 GUI Service 的 wait callback
  *         阻塞等待传输完成。各任务的栈深度与优先级由 app_config.h 集中配置。
  *         GUI Task 与 Playback Task 创建后立即登记进 Filesystem 的调用者拒绝名单
  *         （`Service_Filesystem_DenyCaller`）：它们一调用文件接口就在入口断言停机，Release 也保留。
  *         登记发生在创建之后；它们按设计不调用文件接口，所以创建到登记之间不会有调用。
  */
static void create_task(void *handle)
{
    TaskHandle_t playback_task_handle;
    TaskHandle_t volume_task_handle;
    TaskHandle_t gui_task_handle = NULL;

    (void)handle;

    if (Service_Log_Init() != SERVICE_OK)
    {
        Error_Handler();
    }

    /* 固件版本只在启动时打印一次；队列已就绪，Log Task 创建后消费。
     * 这是刚初始化的空消息池里的第一条，不会被其他任务的早期 INFO 挤掉；此处失败只会是
     * 版本串过长等构建缺陷。诊断行不得阻止播放器启动：失败时静默改投固定短行，
     * 板测时日志里出现 "FW unknown" 即说明版本串有问题。 */
    if (Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "BOOT", "FW " FW_VERSION_STRING " " FW_BUILD_TYPE_STRING) != SERVICE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "BOOT", "FW unknown");
    }

    if (xTaskCreate(log_task,
                    "Log Task",
                    APP_LOG_TASK_STACK_WORDS,
                    NULL,
                    APP_LOG_TASK_PRIORITY,
                    NULL) != pdPASS)
    {
        Error_Handler();
    }

#if STORAGE_SDRAM_BENCHMARK_ENABLE
    /* 内存准备：SDRAM 破坏性自检必须早于任何使用 SDRAM 的任务（Flash 的 FTL 表、块池、曲库）。 */
    storage_sdram_benchmark_run();
#endif

    /* 曲库锁（静态互斥量）：早于创建任何会读写曲库的任务。 */
    catalog_boot_init();

    if (xTaskCreate(storage_task,
                    "Storage Task",
                    APP_STORAGE_TASK_STACK_WORDS,
                    NULL,
                    APP_STORAGE_TASK_PRIORITY,
                    NULL) != pdPASS)
    {
        Error_Handler();
    }

    volume_task_handle = xTaskCreateStatic(volume_task,
                                           "Volume Task",
                                           APP_VOLUME_TASK_STACK_WORDS,
                                           NULL,
                                           APP_VOLUME_TASK_PRIORITY,
                                           volume_task_stack,
                                           &volume_task_tcb);
    if (volume_task_handle == NULL)
    {
        Error_Handler();
    }

    playback_task_handle = xTaskCreateStatic(playback_task,
                                             "Playback Task",
                                             APP_PLAYBACK_TASK_STACK_WORDS,
                                             NULL,
                                             APP_PLAYBACK_TASK_PRIORITY,
                                             playback_task_stack,
                                             &playback_task_tcb);
    if (playback_task_handle == NULL)
    {
        Error_Handler();
    }
    Service_Filesystem_DenyCaller(playback_task_handle);

    if ((xTaskCreate(monitor_task,
                     "Monitor Task",
                     APP_MONITOR_TASK_STACK_WORDS,
                     NULL,
                     APP_MONITOR_TASK_PRIORITY,
                     NULL) != pdPASS) ||
        (xTaskCreate(gui_task,
                     "GUI Task",
                     APP_GUI_TASK_STACK_WORDS,
                     NULL,
                     APP_GUI_TASK_PRIORITY,
                     &gui_task_handle) != pdPASS))
    {
        Error_Handler();
    }
    Service_Filesystem_DenyCaller(gui_task_handle);

    /* 后台任务（优先级 1）：栈在 AXI 新段，静态创建。可以调文件接口，不进拒绝名单。默认构建里没有作业，
     * 它跑到第一次阻塞就一直等通知；钩子打开时它在入口投 HOOK 启动日志（Log Task 此时已创建）。 */
    if (background_task_create() == NULL)
    {
        Error_Handler();
    }

    vTaskDelete(NULL);
}

/**
  * @brief  创建 Create Task 并启动 FreeRTOS 调度器。
  * @note   调度器正常运行后不会返回。若返回，通常表示空闲任务创建失败或内核
  *         堆不足；此时直接进入 CubeMX 的 Error_Handler()。
  */
void app_task_start(void)
{
    if (xTaskCreate(create_task,
                    "Create Task",
                    APP_BOOT_TASK_STACK_WORDS,
                    NULL,
                    APP_BOOT_TASK_PRIORITY,
                    NULL) != pdPASS)
    {
        Error_Handler();
    }

    vTaskStartScheduler();
    Error_Handler();
}
