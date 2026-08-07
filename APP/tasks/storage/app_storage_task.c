/**
  ******************************************************************************
  * @file    app_storage_task.c
  * @brief   SD 卡初始化、热插拔消抖和介质事件处理任务。
  *
  * @details
  *          Storage Task 是当前可移除介质的唯一普通执行上下文。它初始化
  *          Platform SD，在卡检测 EXTI 到达时被直接任务通知唤醒，并在最后一
  *          个边沿后的 30 ms 静默期结束后刷新 SD 卡状态。
  *
  *          GPIO EXTI Adapter 与 Platform SD 不依赖 FreeRTOS。任务通知仅在
  *          本文件的 ISR 回调中出现，因此 Platform 仍可用于裸机或其他运行时。
  *          未来 FatFs 挂载、目录扫描和 USB MSC 所有权仲裁也应由本任务或
  *          后续 Storage Service 串行处理，不能放入 EXTI ISR。
  ******************************************************************************
  */

#include "APP/tasks/storage/app_storage_task.h"

#include <stdint.h>
#include <stdio.h>

#include "Platform/sd/platform_sd.h"
#include "Service/log/log_service.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/** @brief Storage Task 发送 SD 子系统日志时使用的稳定标签。 */
#define STORAGE_SD_LOG_TAG "SD"

/** @brief SD 卡座检测输入在最后一个边沿后的最短稳定时间。 */
#define STORAGE_SD_DEBOUNCE_MS 30U

// /**
//   * @brief  将已经格式化的 Storage 日志投递到 Log Service。
//   * @param  level 日志等级。
//   * @param  text 以 NUL 结尾的日志正文。
//   * @note   Log Service 自身管理静态消息块；本函数不重试队列满，以避免 SD
//   *         热插拔或错误风暴阻塞 Storage Task。
//   */
// static void storage_post_log(LOG_LevelTypeDef level, const char *text)
// {
//     (void)LOG_Service_Post(level, STORAGE_SD_LOG_TAG, text);
// }

/**
  * @brief  读取 Platform SD 最近一次诊断并写入一条错误日志。
  * @param  operation 失败操作的固定说明，例如 "Initialization"。
  * @note   日志正文始终限制在 Log Service 单条消息缓存范围内。
  */
static void storage_post_diagnostics(const char *operation)
{
    Platform_SD_DiagnosticsTypeDef diagnostics;
    char text[128];

    if (Platform_SD_GetDiagnostics(&diagnostics) == PLATFORM_OK)
    {
        (void)snprintf(text,
                       sizeof(text),
                       "%s failed: device=%lu, port=%lu.",
                       operation,
                       (unsigned long)diagnostics.DeviceError,
                       (unsigned long)diagnostics.PortStatus);
    }
    else
    {
        (void)snprintf(text, sizeof(text), "%s failed without diagnostics.", operation);
    }

    // storage_post_log(LOG_LEVEL_ERROR, text);
    (void)LOG_Service_Post(LOG_LEVEL_ERROR, STORAGE_SD_LOG_TAG, text);
}

/**
  * @brief  记录当前已经 READY 的 SD 卡逻辑块信息。
  * @param  prefix 描述本次 READY 原因的固定字符串。
  * @note   Platform_SD_GetInfo() 只复制 Platform 私有缓存；失败时不会读取任何
  *         未初始化的局部变量。
  */
static void storage_log_card_ready(const char *prefix)
{
    Platform_SD_InfoTypeDef info;
    char text[128];

    if (Platform_SD_GetInfo(&info) != PLATFORM_OK)
    {
        // storage_post_log(LOG_LEVEL_ERROR, "Card ready but information is unavailable.");
        (void)LOG_Service_Post(LOG_LEVEL_ERROR, STORAGE_SD_LOG_TAG, "Card ready but information is unavailable.");
        return;
    }

    (void)snprintf(text,
                   sizeof(text),
                   "%s: %lu MB, block size: %lu.",
                   prefix,
                   (unsigned long)(info.CapacityBytes / (1024ULL * 1024ULL)),
                   (unsigned long)info.BlockSize);
    // storage_post_log(LOG_LEVEL_INFO, text);
    (void)LOG_Service_Post(LOG_LEVEL_INFO, STORAGE_SD_LOG_TAG, text);
}

/**
  * @brief  在 SD_CD GPIO EXTI ISR 中通知 Storage Task。
  * @param  context 注册时传入的 Storage TaskHandle_t。
  * @note   Platform SD 通过通用回调调用本函数，因此它不需要且不得认识
  *         FreeRTOS。EXTI9_5 的 NVIC 抢占优先级当前为 10，满足
  *         configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 的 FromISR 调用条件。
  */
static void storage_sd_callback(void *context)
{
    TaskHandle_t task_handle = (TaskHandle_t)context;
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (task_handle != NULL)
    {
        vTaskNotifyGiveFromISR(task_handle, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/**
  * @brief  初始化可移除 SD 卡并记录当前介质状态。
  * @param  task_handle 当前 Storage Task 的有效任务句柄。
  * @note   未插卡是正常状态：Platform_SD_Init() 返回 PLATFORM_OK，状态为
  *         PLATFORM_SD_STATE_NOT_PRESENT。若当前介质初始化失败，Platform
  *         仍会保留 EXTI 注册，后续拔卡和重新插卡可使状态机恢复。
  */
static void storage_init_sd(TaskHandle_t task_handle)
{
    Platform_StatusTypeDef status = Platform_SD_Init(storage_sd_callback, task_handle);
    Platform_SD_StateTypeDef state = Platform_SD_GetState();

    if (status != PLATFORM_OK)
    {
        storage_post_diagnostics("Initialization");
        return;
    }

    if (state == PLATFORM_SD_STATE_NOT_PRESENT)
    {
        // storage_post_log(LOG_LEVEL_INFO, "No card inserted.");
        (void)LOG_Service_Post(LOG_LEVEL_INFO, STORAGE_SD_LOG_TAG, "No card inserted.");
    }
    else if (state == PLATFORM_SD_STATE_READY)
    {
        storage_log_card_ready("Card ready");
    }
    else
    {
        // storage_post_log(LOG_LEVEL_ERROR, "Initialization returned an unexpected SD state.");
        (void)LOG_Service_Post(LOG_LEVEL_ERROR, STORAGE_SD_LOG_TAG, "Initialization returned an unexpected SD state.");
    }
}

/**
  * @brief  刷新一次已经完成消抖的 SD 介质状态，并记录稳定事件。
  * @note   Platform_SD_Process() 不再等待或消抖；它只刷新 Device 状态并生成
  *         INSERTED/REMOVED 语义。此函数必须只由 Storage Task 调用。
  */
static void storage_refresh_sd(void)
{
    Platform_SD_EventTypeDef event;

    if (Platform_SD_Process(&event) != PLATFORM_OK)
    {
        storage_post_diagnostics("Hotplug refresh");
        return;
    }

    if (event == PLATFORM_SD_EVENT_INSERTED)
    {
        storage_log_card_ready("Card inserted");
    }
    else if (event == PLATFORM_SD_EVENT_REMOVED)
    {
        // storage_post_log(LOG_LEVEL_INFO, "Card removed.");
        (void)LOG_Service_Post(LOG_LEVEL_INFO, STORAGE_SD_LOG_TAG, "Card removed.");
    }
}

/**
  * @brief  运行 Storage Task 的 SD 卡热插拔处理循环。
  * @param  handle 当前未使用，保留为 FreeRTOS TaskFunction_t 规定的参数。
  * @note   每次检测边沿都会通知本任务。任务先取得通知，再以 30 ms 超时等待
  *         下一次通知；只要等待期间仍有新边沿，就重新开始完整静默期。超时才
  *         调用 Platform_SD_Process()，因此触点抖动不会触发 SDMMC 或日志操作。
  */
void storage_task(void *handle)
{
    TaskHandle_t task_handle;

    (void)handle;
    task_handle = xTaskGetCurrentTaskHandle();
    storage_init_sd(task_handle);

    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(STORAGE_SD_DEBOUNCE_MS)) != 0U)
        {
            /* 每个新边沿都会重新开始完整的静默消抖窗口。 */
        }

        storage_refresh_sd();
    }
}
