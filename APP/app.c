/**
  ******************************************************************************
  * @file    app.c
  * @brief   应用层初始化顺序与周期任务实现。
  *
  * @details
  *          本文件是 CubeMX 生成代码与自维护模块之间的入口：main.c 负责
  *          MCU 基础设施初始化，app_init() 负责装配日志与板级 PMIC，
  *          app_run() 负责持续推进非阻塞日志发送和 LED 心跳。
  *
  *          当前初始化依赖顺序为：
  *          LOG_Init() -> 启动日志入队 -> Board_PMIC_Init() -> PMIC 结果入队。
  *          日志入队不等于主机已经收到；队列由 app_run() 中的
  *          LOG_Process() 在 USB CDC 可发送后逐条排空。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "app.h"
#include "app_config.h"

#include "main.h"

#include "BSP/Board/board.h"
#include "BSP/Board/sd/board_sd.h"
#include "System/Log/log.h"
/* Variables ------------------------------------------------------------------*/


/* Private functions ---------------------------------------------------------*/
/**
  * @brief  初始化 可移除 SD 卡 并记录当前介质状态。仅实现句柄、ops绑定等操作，不涉及通信。
  * @details
  *         SD 卡不是整机启动的强依赖：没有插卡时 Board_SD_Init() 返回
  *         BOARD_OK，并通过 BOARD_SD_STATE_NOT_PRESENT 表达物理状态；真正的
  *         初始化错误只记录日志，不让播放器进入全局 Error_Handler()。
  * @retval None
  */
static void app_init_sd(void)
{
    Board_StatusTypeDef status = Board_SD_Init();
    Board_SD_StateTypeDef state = Board_SD_GetState();

    if (status != BOARD_OK)
    {
        Board_SD_DiagnosticsTypeDef diagnostics;

        if (Board_SD_GetDiagnostics(&diagnostics) == BOARD_OK)
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

    if (state == BOARD_SD_STATE_NOT_PRESENT)
    {
        (void)LOG_Printf(LOG_LEVEL_INFO,
                         "SD",
                         "no card inserted");
        return;
    }

    if (state == BOARD_SD_STATE_READY)
    {
        Board_SD_InfoTypeDef info;

        /*
         * GetInfo() 只复制 Board 私有缓存；失败时绝不继续使用未初始化的
         * 局部变量，避免旧实现中的未定义容量日志。
         */
        if (Board_SD_GetInfo(&info) == BOARD_OK)
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


/* Exported functions --------------------------------------------------------*/
/**
  * @brief  初始化应用服务和板级设备。
  * @note   LOG_Init() 失败时后续 LOG_Printf() 会返回 LOG_ERROR；当前代码
  *         有意忽略日志返回值，因为日志不能阻止电源初始化继续执行。
  * @note   PMIC 初始化失败属于致命错误，会调用 CubeMX 的 Error_Handler()。
  *         Error_Handler() 会关闭中断，因此刚入队的失败日志不保证能通过
  *         USB 发出；此时应同时查看 hpmic 或使用调试器定位。
  * @retval None
  */
void app_init(void)
{
    /* 初始化日志对象并绑定 USB CDC 输出端口和 HAL 毫秒时间源。 */
    (void)LOG_Init();

    /* 此时 USB 可能尚未被主机打开；日志先复制进 RAM 队列等待发送。 */
    (void)LOG_Printf(LOG_LEVEL_INFO, "LOG", "initialization successful");

    /* Board 层负责初始化板级设备。 */
    Board_StatusTypeDef board_status = Board_Init();

    switch (board_status)
    {
        case BOARD_OK:
            (void)LOG_Printf(LOG_LEVEL_INFO, "BOARD", "Board initialization successful");
            app_init_sd();
            break;

        default:
            (void)LOG_Printf(LOG_LEVEL_ERROR, "BOARD", "Board initialization failed");
            Error_Handler();
            break;
    }
}

/**
  * @brief  执行一次应用周期任务。
  * @note   本函数采用“高频轮询 + 时间差判断”，不会调用 HAL_Delay()，
  *         因而 USB 日志队列可持续得到处理。
  * @retval None
  */
void app_run(void)
{
    /* static 变量跨调用保存上次翻转时刻，上电清零后不占用栈空间。 */
    static uint32_t last_led_toggle_ms = 0U;
    uint32_t now_ms = HAL_GetTick();

    /*
     * 每次最多处理一条日志：USB 未就绪或正忙时立即返回，队首保持不变。
     * 因此主循环调用频率越高，USB 空闲后队列排空得越及时。
     */
    (void)LOG_Process();

    /*
     * 无符号减法可以正确跨越 HAL_GetTick() 的 32 位自然回绕点；只要判断
     * 周期远小于 2^31 ms，就不需要为 tick 溢出编写特殊分支。
     */
    if ((uint32_t)(now_ms - last_led_toggle_ms) >= 500U)
    {
        last_led_toggle_ms = now_ms;
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

        if (Board_SD_Refresh() != BOARD_OK) // * 扫描 SD 卡是否插上，插上就初始化
        {
            /* 可读取诊断信息，但不要直接进入 Error_Handler。 */
        }
    }
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
