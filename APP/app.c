/**
  ******************************************************************************
  * @file    app.c
  * @brief   应用层初始化顺序与周期任务实现。
  *
  * @details
  *          本文件是 CubeMX 生成代码与自维护模块之间的入口：main.c 负责
  *          MCU 基础设施初始化；app_init() 负责初始化日志、整机强依赖
  *          Platform Module 并启动 FreeRTOS 任务。可移除 SD 卡的初始化、
  *          热插拔消抖和后续文件系统工作由 Storage Task 独占处理。
  *
  *          当前初始化依赖顺序为：
  *          Platform_Log_Init() -> 启动日志入队 -> Platform_Init() -> app_task_start()。
  *          Platform_Init() 内部初始化 GPIO EXTI Adapter、PMIC、音频电源和
  *          Audio Device；Storage Task 在 Log Service 就绪后处理可选 SD 卡。
  *          启动日志先保存在 Components/log 环形队列，调度器启动后由 Log Task
  *          与 Log Service 逐步排空。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "app.h"
#include "app_config.h"

#include "APP/tasks/app_tasks.h"

#include "main.h"

#include "Platform/platform.h"
#include "Platform/log/platform_log.h"
#include "Platform/power/platform_power.h"
#include "Components/log/log.h"
/* Variables ------------------------------------------------------------------*/

/* Private functions ---------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  初始化应用服务和板级设备。
  * @note   Platform_Log_Init() 失败属于基础诊断能力初始化失败，当前实现直接进入
  *         Error_Handler()，避免系统在完全不可观察的状态下继续启动。
  * @note   PMIC 初始化失败属于致命错误，会调用 CubeMX 的 Error_Handler()。
  *         Error_Handler() 会关闭中断，因此刚入队的失败日志不保证能通过
  *         USB 发出；此时应同时查看 hpmic 或使用调试器定位。
  * @retval None
  */
void app_init(void)
{
    /* Platform 负责选择并装配 USB CDC 输出和 HAL 毫秒时间源。 */
    if (Platform_Log_Init() != PLATFORM_OK)
    {
        Error_Handler();
    }

    /* 此时 USB 可能尚未被主机打开；日志先复制进 RAM 队列等待发送。 */
    (void)LOG_Printf(LOG_LEVEL_INFO, "LOG", "Initialization successful.");

    /* Platform 层负责初始化板级设备。 */
    Platform_StatusTypeDef platform_status = Platform_Init();

    switch (platform_status)
    {
        case PLATFORM_OK:
            (void)LOG_Printf(LOG_LEVEL_INFO, "PLATFORM", "Initialization successful.");
            break;

        case PLATFORM_PMIC_ERROR:
        {
            Platform_Power_DiagnosticsTypeDef diagnostics;

            if (Platform_Power_GetDiagnostics(&diagnostics) == PLATFORM_OK)
            {
                (void)LOG_Printf(
                    LOG_LEVEL_ERROR,
                    "POWER",
                    "Initialization failed: state=%lu, error=%lu, bus=%lu, reg=0x%02X.",
                    (unsigned long)diagnostics.DeviceState,
                    (unsigned long)diagnostics.DeviceError,
                    (unsigned long)diagnostics.BusStatus,
                    (unsigned int)diagnostics.FailedRegister);
            }

            Error_Handler();
            break;
        }

        default:
            (void)LOG_Printf(LOG_LEVEL_ERROR, "PLATFORM", "Initialization failed.");
            Error_Handler();
            break;
    }

    app_task_start();
}

/**
  * @brief  应用层错误处理扩展入口。
  * @note   当前没有额外动作。后续若加入安全关断，应避免在这里调用依赖
  *         已关闭中断的异步 USB 输出。
  * @retval None
  */
void app_error(void)
{
    /* Reserved for future application-specific fail-safe handling. */
}
