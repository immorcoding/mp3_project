/**
  ******************************************************************************
  * @file    app.c
  * @brief   应用层初始化顺序与周期任务实现。
  *
  * @details
  *          本文件是 CubeMX 生成代码与自维护模块之间的入口：main.c 负责
  *          MCU 基础设施初始化；app_init() 负责初始化日志、整机强依赖
  *          Platform Module 和可选 SD 卡；app_run() 负责持续推进非阻塞日志、
  *          SD 热插拔处理和 LED 心跳。
  *
  *          当前初始化依赖顺序为：
  *          Platform_Log_Init() -> 启动日志入队 -> Platform_Init() -> Platform_SD_Init()。
  *          Platform_Init() 内部初始化 IRQ Dispatcher、PMIC、音频电源和 Audio
  *          Device；SD 卡是可选介质，其初始化失败不会进入全局 Error_Handler()。
  *          日志入队不等于主机已经收到；队列由 app_run() 中的 LOG_Process()
  *          在 USB CDC 可发送后逐条排空。
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
#include "Platform/sd/platform_sd.h"
#include "Components/log/log.h"
/* Variables ------------------------------------------------------------------*/
/** @brief 标记 Platform SD 模块是否已成功初始化，可否进入周期处理。 */
static bool app_sd_initialized;

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  初始化可移除 SD 卡并记录当前介质状态。
  * @details
  *         SD 卡不是整机启动的强依赖：没有插卡时 Platform_SD_Init() 返回
  *         PLATFORM_OK，并通过 PLATFORM_SD_STATE_NOT_PRESENT 表达物理状态；真正的
  *         初始化错误只记录日志，不让播放器进入全局 Error_Handler()。
  *         插卡启动时 Platform_SD_Init() 会初始化 SDMMC、识别介质并读取块信息。
  * @retval None
  */
static void app_init_sd(void)
{
    Platform_StatusTypeDef status = Platform_SD_Init();
    Platform_SD_StateTypeDef state = Platform_SD_GetState();

    app_sd_initialized = false;

    if (status != PLATFORM_OK)
    {
        Platform_SD_DiagnosticsTypeDef diagnostics;

        if (Platform_SD_GetDiagnostics(&diagnostics) == PLATFORM_OK)
        {
            (void)LOG_Printf(LOG_LEVEL_ERROR,
                             "SD",
                             "initialization failed: device=%lu, port=%lu",
                             (unsigned long)diagnostics.DeviceError,
                             (unsigned long)diagnostics.PortStatus);
        }
        else
        {
            (void)LOG_Printf(LOG_LEVEL_ERROR,
                             "SD",
                             "initialization failed without diagnostics");
        }

        return;
    }

    app_sd_initialized = true;

    if (state == PLATFORM_SD_STATE_NOT_PRESENT)
    {
        (void)LOG_Printf(LOG_LEVEL_INFO,
                         "SD",
                         "no card inserted");
        return;
    }

    if (state == PLATFORM_SD_STATE_READY)
    {
        Platform_SD_InfoTypeDef info;

        /*
         * GetInfo() 只复制 Platform 私有缓存；失败时绝不继续使用未初始化的
         * 局部变量，避免旧实现中的未定义容量日志。
         */
        if (Platform_SD_GetInfo(&info) == PLATFORM_OK)
        {
            uint32_t capacity_mb = (uint32_t)(info.CapacityBytes / (1024ULL * 1024ULL));

            (void)LOG_Printf(LOG_LEVEL_INFO,
                             "SD",
                             "card ready: %lu MB, block size: %lu",
                             (unsigned long)capacity_mb,
                             (unsigned long)info.BlockSize);
        }
        else
        {
            (void)LOG_Printf(LOG_LEVEL_ERROR,
                             "SD",
                             "card ready but information is unavailable");
        }
    }
}

// /**
//   * @brief  推进 SD 卡热插拔消抖并处理一次稳定状态变化。
//   * @note   Platform SD 只报告介质事件；日志和未来的文件系统挂载策略由应用层负责。
//   * @retval None
//   */
// static void app_process_sd(void)
// {
//     Platform_SD_EventTypeDef event;

//     if (!app_sd_initialized)
//     {
//         return;
//     }

//     if (Platform_SD_Process(&event) != PLATFORM_OK)
//     {
//         Platform_SD_DiagnosticsTypeDef diagnostics;

//         if (Platform_SD_GetDiagnostics(&diagnostics) == PLATFORM_OK)
//         {
//             (void)LOG_Printf(LOG_LEVEL_ERROR,
//                              "SD",
//                              "hotplug refresh failed: device=%lu, port=%lu",
//                              (unsigned long)diagnostics.DeviceError,
//                              (unsigned long)diagnostics.PortStatus);
//         }

//         return;
//     }

//     if (event == PLATFORM_SD_EVENT_INSERTED)
//     {
//         Platform_SD_InfoTypeDef info;

//         if (Platform_SD_GetInfo(&info) == PLATFORM_OK)
//         {
//             uint32_t capacity_mb = (uint32_t)(info.CapacityBytes / (1024ULL * 1024ULL));

//             (void)LOG_Printf(LOG_LEVEL_INFO,
//                              "SD",
//                              "card inserted: %lu MB, block size: %lu",
//                              (unsigned long)capacity_mb,
//                              (unsigned long)info.BlockSize);
//         }
//         else
//         {
//             (void)LOG_Printf(LOG_LEVEL_ERROR,
//                              "SD",
//                              "card inserted but information is unavailable");
//         }
//     }
//     else if (event == PLATFORM_SD_EVENT_REMOVED)
//     {
//         (void)LOG_Printf(LOG_LEVEL_INFO, "SD", "card removed");
//     }
// }


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

    // /* 此时 USB 可能尚未被主机打开；日志先复制进 RAM 队列等待发送。 */
    // (void)LOG_Printf(LOG_LEVEL_INFO, "LOG", "initialization successful");

    // /* Platform 层负责初始化板级设备。 */
    // Platform_StatusTypeDef platform_status = Platform_Init();

    // switch (platform_status)
    // {
    //     case PLATFORM_OK:
    //         (void)LOG_Printf(LOG_LEVEL_INFO, "PLATFORM", "initialization successful");
    //         app_init_sd();
    //         break;

    //     case PLATFORM_PMIC_ERROR:
    //     {
    //         Platform_Power_DiagnosticsTypeDef diagnostics;

    //         if (Platform_Power_GetDiagnostics(&diagnostics) == PLATFORM_OK)
    //         {
    //             (void)LOG_Printf(
    //                 LOG_LEVEL_ERROR,
    //                 "POWER",
    //                 "initialization failed: state=%lu, error=%lu, bus=%lu, reg=0x%02X",
    //                 (unsigned long)diagnostics.DeviceState,
    //                 (unsigned long)diagnostics.DeviceError,
    //                 (unsigned long)diagnostics.BusStatus,
    //                 (unsigned int)diagnostics.FailedRegister);
    //         }

    //         Error_Handler();
    //         break;
    //     }

    //     default:
    //         (void)LOG_Printf(LOG_LEVEL_ERROR, "PLATFORM", "initialization failed");
    //         Error_Handler();
    //         break;
    // }

    app_tasks_init();
}

/**
  * @brief  执行一次应用周期任务。
  * @note   本函数采用“高频轮询 + 时间差判断”，不会调用 HAL_Delay()，
  *         因而 USB 日志队列可持续得到处理。
  * @retval None
  */
// void app_run(void)
// {
//     /* static 变量跨调用保存上次翻转时刻，上电清零后不占用栈空间。 */
//     static uint32_t last_led_toggle_ms = 0U;
//     uint32_t now_ms = HAL_GetTick();

//     /*
//      * 每次最多处理一条日志：USB 未就绪或正忙时立即返回，队首保持不变。
//      * 因此主循环调用频率越高，USB 空闲后队列排空得越及时。
//      */
//     (void)LOG_Process();
//     app_process_sd();

//     /*
//      * 无符号减法可以正确跨越 HAL_GetTick() 的 32 位自然回绕点；只要判断
//      * 周期远小于 2^31 ms，就不需要为 tick 溢出编写特殊分支。
//      */
//     if ((uint32_t)(now_ms - last_led_toggle_ms) >= 500U)
//     {
//         last_led_toggle_ms = now_ms;
//         HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
//     }
// }

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
