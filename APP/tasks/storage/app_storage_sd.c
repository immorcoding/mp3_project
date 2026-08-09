/**
  ******************************************************************************
  * @file    app_storage_sd.c
  * @brief   Storage Task 的 SD 介质生命周期和 FatFs 卷管理。
  *
  * @details
  *          本模块是 Storage Task 与 Platform SD、Filesystem Service 之间的
  *          组合层。GPIO EXTI 只通过直接任务通知唤醒 Storage Task；消抖完成后，
  *          本模块才初始化/刷新 SD 卡并挂载或卸载 FatFs。
  ******************************************************************************
  */

#include "APP/tasks/storage/app_storage_sd.h"

#include <stdint.h>
#include <stdio.h>

#include "Platform/sd/platform_sd.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/log/log_service.h"

/** @brief Storage Task 发送 SD 子系统日志时使用的稳定标签。 */
#define STORAGE_SD_LOG_TAG "SD"

/** @brief 已执行 storage_sd_init() 的 Storage Task，用于保护破坏性操作的调用上下文。 */
static TaskHandle_t storage_sd_task_handle;

/**
  * @brief  读取 Platform SD 最近一次诊断并写入一条错误日志。
  * @param  operation 失败操作的固定说明，例如 "Initialization"。
  * @note   日志正文始终限制在 Log Service 单条消息缓存范围内。
  */
static void storage_sd_post_diagnostics(const char *operation)
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

    (void)LOG_Service_Post(LOG_LEVEL_ERROR, STORAGE_SD_LOG_TAG, text);
}

/**
  * @brief  记录当前已经 READY 的 SD 卡逻辑块信息。
  * @param  prefix 描述本次 READY 原因的固定字符串。
  */
static void storage_sd_log_card_ready(const char *prefix)
{
    Platform_SD_InfoTypeDef info;
    char text[128];

    if (Platform_SD_GetInfo(&info) != PLATFORM_OK)
    {
        (void)LOG_Service_Post(LOG_LEVEL_ERROR,
                               STORAGE_SD_LOG_TAG,
                               "Card ready but information is unavailable.");
        return;
    }

    (void)snprintf(text,
                   sizeof(text),
                   "%s: %lu MB, block size: %lu.",
                   prefix,
                   (unsigned long)(info.CapacityBytes / (1024ULL * 1024ULL)),
                   (unsigned long)info.BlockSize);
    (void)LOG_Service_Post(LOG_LEVEL_INFO, STORAGE_SD_LOG_TAG, text);
}

/**
  * @brief  确认 CubeMX DiskIO Driver 已链接，并为后续 FatFs 操作报告失败。
  * @retval FR_OK 可继续执行 FatFs 操作。
  * @retval 其他值 Driver 未就绪。
  */
static FRESULT storage_sd_prepare_filesystem(void)
{
    FRESULT result = Filesystem_Service_Init();

    if (result != FR_OK)
    {
        (void)LOG_Service_Post(LOG_LEVEL_ERROR,
                               STORAGE_SD_LOG_TAG,
                               "FatFs driver is not ready.");
    }

    return result;
}

/**
  * @brief  挂载当前已经由 Platform SD 初始化完成的 FAT 卷。
  * @note   未格式化或文件系统类型不受支持的卡会返回 FR_NO_FILESYSTEM，这不表示
  *         SDMMC 通信链路失败。
  */
static void storage_sd_mount(void)
{
    FRESULT result;

    result = storage_sd_prepare_filesystem();
    if (result != FR_OK)
    {
        return;
    }

    result = Filesystem_Service_MountSD();
    if (result == FR_OK)
    {
        (void)LOG_Service_Post(LOG_LEVEL_INFO, STORAGE_SD_LOG_TAG, "Filesystem mounted.");
    }
    else if (result == FR_NO_FILESYSTEM)
    {
        (void)LOG_Service_Post(LOG_LEVEL_WARN,
                               STORAGE_SD_LOG_TAG,
                               "No FAT filesystem found.");
    }
    else
    {
        (void)LOG_Service_Post(LOG_LEVEL_ERROR,
                               STORAGE_SD_LOG_TAG,
                               "Filesystem mount failed.");
    }
}

/**
  * @brief  注销当前 SD 的 FatFs 卷对象。
  * @retval FR_OK 卷对象已注销，或此前尚未成功挂载。
  * @retval 其他值 注销失败。
  * @note   此操作不访问已移除的 SD 卡，只解除 FatFs 与逻辑卷的关联。
  */
static FRESULT storage_sd_unmount(void)
{
    FRESULT result;

    result = storage_sd_prepare_filesystem();
    if (result != FR_OK)
    {
        return result;
    }

    result = Filesystem_Service_UnmountSD();
    if (result != FR_OK)
    {
        (void)LOG_Service_Post(LOG_LEVEL_ERROR,
                               STORAGE_SD_LOG_TAG,
                               "Filesystem unmount failed.");
    }

    return result;
}

/**
  * @brief  在 SD_CD GPIO EXTI ISR 中通知 Storage Task。
  * @param  context 注册时传入的 Storage TaskHandle_t。
  * @note   EXTI9_5 的 NVIC 抢占优先级当前为 10，满足 FreeRTOS FromISR 调用条件。
  */
static void storage_sd_detect_callback(void *context)
{
    TaskHandle_t task_handle = (TaskHandle_t)context;
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (task_handle != NULL)
    {
        vTaskNotifyGiveFromISR(task_handle, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

void storage_sd_init(TaskHandle_t task_handle)
{
    Platform_StatusTypeDef status;
    Platform_SD_StateTypeDef state;

    if (task_handle == NULL)
    {
        (void)LOG_Service_Post(LOG_LEVEL_ERROR,
                               STORAGE_SD_LOG_TAG,
                               "Storage task handle is invalid.");
        return;
    }

    storage_sd_task_handle = task_handle;
    status = Platform_SD_Init(storage_sd_detect_callback, task_handle);
    state = Platform_SD_GetState();

    if (status != PLATFORM_OK)
    {
        storage_sd_post_diagnostics("Initialization");
        return;
    }

    if (state == PLATFORM_SD_STATE_NOT_PRESENT)
    {
        (void)LOG_Service_Post(LOG_LEVEL_INFO, STORAGE_SD_LOG_TAG, "No card inserted.");
    }
    else if (state == PLATFORM_SD_STATE_READY)
    {
        storage_sd_log_card_ready("Card ready");
        storage_sd_mount();
    }
    else
    {
        (void)LOG_Service_Post(LOG_LEVEL_ERROR,
                               STORAGE_SD_LOG_TAG,
                               "Initialization returned an unexpected SD state.");
    }
}

void storage_sd_process(void)
{
    Platform_SD_EventTypeDef event;

    if (Platform_SD_Process(&event) != PLATFORM_OK)
    {
        storage_sd_post_diagnostics("Hotplug refresh");
        return;
    }

    if (event == PLATFORM_SD_EVENT_INSERTED)
    {
        storage_sd_log_card_ready("Card inserted");
        storage_sd_mount();
    }
    else if (event == PLATFORM_SD_EVENT_REMOVED)
    {
        (void)storage_sd_unmount();
        (void)LOG_Service_Post(LOG_LEVEL_INFO, STORAGE_SD_LOG_TAG, "Card removed.");
    }
}

void storage_sd_format_and_mount(void)
{
    FRESULT result;

    if ((storage_sd_task_handle == NULL) ||
        (xTaskGetCurrentTaskHandle() != storage_sd_task_handle))
    {
        (void)LOG_Service_Post(LOG_LEVEL_ERROR,
                               STORAGE_SD_LOG_TAG,
                               "Format request rejected outside Storage Task.");
        return;
    }

    if (Platform_SD_GetState() != PLATFORM_SD_STATE_READY)
    {
        (void)LOG_Service_Post(LOG_LEVEL_ERROR,
                               STORAGE_SD_LOG_TAG,
                               "Format request rejected: card is not ready.");
        return;
    }

    result = storage_sd_unmount();
    if (result != FR_OK)
    {
        return;
    }

    result = Filesystem_Service_MkfsSD();
    if (result != FR_OK)
    {
        (void)LOG_Service_Post(LOG_LEVEL_ERROR,
                               STORAGE_SD_LOG_TAG,
                               "Filesystem format failed.");
        return;
    }

    (void)LOG_Service_Post(LOG_LEVEL_INFO, STORAGE_SD_LOG_TAG, "Filesystem formatted.");
    storage_sd_mount();
}
