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
#include "System/Log/log.h"

#include "APP/Audio/audio_test_pcm.h"

/* Variables ------------------------------------------------------------------*/


/* Exported functions --------------------------------------------------------*/
#define APP_AUDIO_TX_CHUNK_SAMPLES  (65534U)

static Board_StatusTypeDef app_audio_test(void)
{
    uint32_t offset = 0U;

    /* PCM5102A XSMT 拉高，解除静音。 */
    if (Board_Audio_SetMute(false) != BOARD_OK)
    {
        return BOARD_AUDIO_ERROR;
    }
    (void)LOG_Printf(LOG_LEVEL_INFO, "BOARD", "Audio mute off.");

    while (offset < AUDIO_TEST_PCM_SAMPLE_COUNT)
    {
        uint32_t remaining = AUDIO_TEST_PCM_SAMPLE_COUNT - offset;

        /*
         * HAL_I2S_Transmit() 的 Size 是 uint16_t。
         * 使用 65534 而不是 65535，是为了保证每块包含偶数个
         * 16-bit 样本，不破坏 L/R 立体声配对。
         */
        uint16_t chunk_size =
            (remaining > APP_AUDIO_TX_CHUNK_SAMPLES)
            ? APP_AUDIO_TX_CHUNK_SAMPLES
            : (uint16_t)remaining;

        if (Board_Audio_Transmit(&g_audio_test_pcm[offset],
                                 chunk_size) != BOARD_OK)
        {
            (void)Board_Audio_SetMute(true);
            return BOARD_AUDIO_ERROR;
        }

        offset += chunk_size;
    }

    /* 播放结束后重新静音。 */
    // (void)Board_Audio_SetMute(true);

    return BOARD_OK;
}

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
            break;

        default:
            (void)LOG_Printf(LOG_LEVEL_ERROR, "BOARD", "Board initialization failed");
            Error_Handler();
            break;
    }

    board_status = app_audio_test();
    switch (board_status)
    {
        case BOARD_OK:
            (void)LOG_Printf(LOG_LEVEL_INFO, "BOARD", "Board Audio test successful");
            break;

        default:
            (void)LOG_Printf(LOG_LEVEL_ERROR, "BOARD", "Board Audio test failed");
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
