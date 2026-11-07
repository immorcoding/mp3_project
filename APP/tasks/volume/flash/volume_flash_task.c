/**
 ******************************************************************************
 * @file    volume_flash_task.c
 * @brief   一次性的 Flash 启动任务：跑 `volume_flash_boot()`，自删前自己打一行栈水位。
 *
 * @details
 *          卷任务创建它、不等它，优先级 2（低于卷任务的 3），所以 Flash 启动期间（`MountFlash` 3.3 至 8.3 s）
 *          卷任务照常处理插拔卡；这个任务只持 Flash 卷锁，与 SD 卷锁互不相干（STOR-9、R3.5）。
 *          静态创建：TCB 在 DTCM `.bss`，约 4 KiB 栈放 AXI SRAM 新段 `.app_axi_ram`（NOLOAD，只经 CPU 读写，
 *          不作 DMA）。任务自删之后 Monitor 读不到它的栈，所以自删前自己打一行水位，格式与 Monitor 的栈行相同。
 *          与 ARC-17 字面不符的静态栈留给补丁版本改回动态（地图评论、ARC-17 的 Signal）。
 ******************************************************************************
 */

#include "APP/tasks/volume/flash/volume_flash.h"
#include "APP/app_config.h"

#include <stdint.h>
#include <stdio.h>

#include "Service/log/log_service.h"

/*
 * HWD-2：.app_axi_ram 是 NOLOAD，不自动清零。静态栈在 xTaskCreateStatic 里被整块填成已知值
 * （tskSET_NEW_STACKS_TO_KNOWN_VALUE，由下面两个配置之一打开），首次使用前因此完整写入。
 * 配置变化让这个前提失效时，在编译期报错，而不是留下一块没写过的栈。
 */
_Static_assert((configCHECK_FOR_STACK_OVERFLOW > 1) || (configUSE_TRACE_FACILITY == 1),
               "static task stacks in NOLOAD .app_axi_ram rely on the kernel filling them at creation (HWD-2)");

/* 一次性任务的 TCB 静态持有，位于 DTCM .bss，不占内核堆。 */
static StaticTask_t volume_flash_task_tcb;
static StackType_t volume_flash_task_stack[APP_FLASH_BOOT_TASK_STACK_WORDS]
    __attribute__((section(".app_axi_ram"), aligned(8)));

/**
 * @brief  打一行自己的栈水位：总量、历史最小剩余、峰值占比（与 Monitor 的栈行同格式，标签 VOLUME）。
 * @note   在任务自删之前调用；水位是创建以来的最小剩余栈，包含 MountFlash / 格式化的最深调用链。
 */
static void volume_flash_task_post_stack_line(void)
{
    TaskStatus_t status;
    char text[96];
    unsigned long total_bytes;
    unsigned long free_bytes;
    unsigned long peak_x10;

    vTaskGetInfo(NULL, &status, pdTRUE, eRunning);
    total_bytes = (unsigned long)(APP_FLASH_BOOT_TASK_STACK_WORDS * sizeof(StackType_t));
    free_bytes = (unsigned long)((unsigned long)status.usStackHighWaterMark * sizeof(StackType_t));
    peak_x10 = (free_bytes > total_bytes) ? 0UL : (((total_bytes - free_bytes) * 1000UL) / total_bytes);

    (void)snprintf(text,
                   sizeof(text),
                   "Flash Boot Task: total=%lu B, min free=%lu B, peak=%lu.%lu%%",
                   total_bytes,
                   free_bytes,
                   peak_x10 / 10UL,
                   peak_x10 % 10UL);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "VOLUME", text);
}

/**
 * @brief  Flash 启动任务入口：跑完启动流程、打栈水位、自删。
 * @param[in] handle 未使用，保留以满足 FreeRTOS TaskFunction_t。
 */
static void volume_flash_boot_task(void *handle)
{
    (void)handle;
    (void)volume_flash_boot(); /* 失败也只记日志（volume_flash_boot 已记），不重试。 */
    volume_flash_task_post_stack_line();
    vTaskDelete(NULL);
}

/**
 * @brief  静态创建 Flash 启动任务。
 * @return 任务句柄；创建失败为 NULL（静态创建只在参数为空时失败，实际不会发生）。
 * @note   由卷任务入口调用，先于初始化 SD：Flash 启动期间卷任务不等它。
 */
TaskHandle_t volume_flash_task_create(void)
{
    return xTaskCreateStatic(volume_flash_boot_task,
                             "Flash Boot Task",
                             APP_FLASH_BOOT_TASK_STACK_WORDS,
                             NULL,
                             APP_FLASH_BOOT_TASK_PRIORITY,
                             volume_flash_task_stack,
                             &volume_flash_task_tcb);
}
