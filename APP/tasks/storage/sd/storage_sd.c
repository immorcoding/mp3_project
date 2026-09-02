/**
  ******************************************************************************
  * @file    storage_sd.c
  * @brief   Storage Task 的 SD 介质生命周期和 FatFs 卷管理。
  *
  * @details
  *          本模块是 Storage Task 与 Platform SD、Filesystem Service 之间的
  *          组合层。GPIO EXTI 只通过直接任务通知唤醒 Storage Task；消抖完成后，
  *          本模块才初始化/刷新 SD 卡并挂载或卸载 FatFs。
  ******************************************************************************
  */

#include "APP/tasks/storage/sd/storage_sd.h"
#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/storage/benchmark/storage_sd_benchmark.h"
#include "APP/app_config.h"

#include <stdint.h>
#include <stdio.h>

#include "Platform/sd/platform_sd.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/log/log_service.h"

/** @brief Storage Task 发送 SD 子系统日志时使用的稳定标签。 */
static const char storage_sd_log_tag[] = "SD";

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

    (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, storage_sd_log_tag, text);
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
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_sd_log_tag,
                               "Card ready but information is unavailable.");
        return;
    }

    (void)snprintf(text,
                   sizeof(text),
                   "%s: %lu MB, block size: %lu.",
                   prefix,
                   (unsigned long)(info.CapacityBytes / (1024ULL * 1024ULL)),
                   (unsigned long)info.BlockSize);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, storage_sd_log_tag, text);
}

/**
  * @brief  确认 CubeMX DiskIO Driver 已链接，并为后续 FatFs 操作报告失败。
  * @retval SERVICE_OK 可继续执行 FatFs 操作。
  * @retval 其他值 Driver 未就绪。
  */
static Service_StatusTypeDef storage_sd_prepare_filesystem(void)
{
    Service_StatusTypeDef result = Service_Filesystem_InitSD(STORAGE_NOTIFY_SD_TRANSFER);

    if (result != SERVICE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_sd_log_tag,
                               "FatFs driver is not ready.");
    }

    return result;
}

/**
  * @brief  挂载当前已经由 Platform SD 初始化完成的 FAT 卷。
 * @retval SERVICE_OK 当前卷已挂载。
 * @retval 其他值 挂载失败；未格式化或文件系统类型不受支持的卡通常返回
 *         SERVICE_NO_FILESYSTEM，这不表示 SDMMC 通信链路失败。
 */
static Service_StatusTypeDef storage_sd_mount(void)
{
    Service_StatusTypeDef result;

    result = storage_sd_prepare_filesystem();
    if (result != SERVICE_OK)
    {
        return result;
    }

    result = Service_Filesystem_MountSD();
    if (result == SERVICE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, storage_sd_log_tag, "Filesystem mounted.");
    }
    else if (result == SERVICE_NO_FILESYSTEM)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                               storage_sd_log_tag,
                               "No FAT filesystem found.");
    }
    else
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_sd_log_tag,
                               "Filesystem mount failed.");
    }

    return result;
}

/**
  * @brief  注销当前 SD 的 FatFs 卷对象。
  * @retval SERVICE_OK 卷对象已注销，或此前尚未成功挂载。
  * @retval 其他值 注销失败。
  * @note   此操作不访问已移除的 SD 卡，只解除 FatFs 与逻辑卷的关联。
  */
static Service_StatusTypeDef storage_sd_unmount(void)
{
    Service_StatusTypeDef result;

    result = storage_sd_prepare_filesystem();
    if (result != SERVICE_OK)
    {
        return result;
    }

    result = Service_Filesystem_UnmountSD();
    if (result != SERVICE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_sd_log_tag,
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
        vTaskNotifyGiveIndexedFromISR(task_handle,
                                      STORAGE_NOTIFY_SD_DETECT,
                                      &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/**
  * @brief  初始化 Platform SD，并处理启动时已经插入的介质。
  * @param  task_handle 当前 Storage Task 的有效任务句柄。
  * @note   本函数把 EXTI 轻量通知绑定到 task_handle。必须在该任务上下文调用；
  *         句柄为空或与当前任务不符时拒绝。无卡属于正常状态；已插卡
  *         时会在 Storage Task 上下文尝试挂载文件系统。
  */
void storage_sd_init(TaskHandle_t task_handle)
{
    Platform_StatusTypeDef status;
    Platform_SD_StateTypeDef state;

    if (task_handle == NULL)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_sd_log_tag,
                               "Storage task handle is invalid.");
        return;
    }

    if (task_handle != xTaskGetCurrentTaskHandle())
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_sd_log_tag,
                               "Storage task handle is invalid.");
        return;
    }

    status = Platform_SD_Init(storage_sd_detect_callback,
                              task_handle);
    state = Platform_SD_GetState();

    if (status != PLATFORM_OK)
    {
        storage_sd_post_diagnostics("Initialization");
        return;
    }

    if (storage_sd_prepare_filesystem() != SERVICE_OK)
    {
        return;
    }

    if (state == PLATFORM_SD_STATE_NOT_PRESENT)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, storage_sd_log_tag, "No card inserted.");
    }
    else if (state == PLATFORM_SD_STATE_READY)
    {
        storage_sd_log_card_ready("Card ready");
        if (storage_sd_mount() == SERVICE_OK)
        {
#if STORAGE_SD_BENCHMARK_ENABLE /* SD 读写基准测试。 */
            (void)storage_sd_benchmark_run();
#endif
        }
    }
    else
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_sd_log_tag,
                               "Initialization returned an unexpected SD state.");
    }
}

/**
  * @brief  处理已完成消抖的一次 SD 卡检测事件。
  * @note   仅能在 Storage Task 普通上下文调用。插卡后挂载，拔卡后先注销 FatFs
  *         卷对象；本函数不执行机械触点消抖。
  */
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
        (void)storage_sd_mount();
    }
    else if (event == PLATFORM_SD_EVENT_REMOVED)
    {
        (void)storage_sd_unmount();
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, storage_sd_log_tag, "Card removed.");
    }
}
