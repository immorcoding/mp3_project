#include "Platform/platform.h"

#include "Platform/audio/platform_audio.h"
#include "Platform/platform_irq.h"
#include "Platform/power/platform_power.h"

#include "stm32h7xx_hal.h"

/**
  * @brief  初始化整机启动所必需的 Platform Module。
  * @retval PLATFORM_OK          初始化成功。
  * @retval PLATFORM_PMIC_ERROR  PMIC 初始化失败。
  * @retval PLATFORM_AUDIO_ERROR 音频供电或音频设备初始化失败。
  */
Platform_StatusTypeDef Platform_Init(void)
{
    /* IRQ Dispatcher 必须先于所有可能注册板级中断的 Module 初始化。 */
    if (Platform_IRQ_Init() != PLATFORM_OK)
    {
        return PLATFORM_IRQ_ERROR;
    }

    /* 初始化本板电源管理和 AXP2101 启动策略。 */
    if (Platform_Power_Init() != PLATFORM_OK)
    {
        return PLATFORM_PMIC_ERROR;
    }

    /* AUDIO_POWER 由 AXP2101 ALDO1 供电，必须在音频设备初始化之前开启。 */
    if (Platform_Power_SetAudio(true) != PLATFORM_OK)
    {
        return PLATFORM_AUDIO_ERROR;
    }

    /* 等待音频电源轨和 PCM5102A 模拟部分稳定。 */
    HAL_Delay(500);

    /* 初始化 Audio */
    if (Platform_Audio_Init() != PLATFORM_OK)
    {
        return PLATFORM_AUDIO_ERROR;
    }

    return PLATFORM_OK;
}
