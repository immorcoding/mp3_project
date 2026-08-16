/**
  ******************************************************************************
  * @file    platform.c
  * @brief   整机强依赖硬件能力的 Platform 初始化顺序实现。
  ******************************************************************************
  */

#include "Platform/platform.h"

#include "Platform/audio/platform_audio.h"
#include "Platform/lcd/platform_lcd.h"
#include "Platform/power/platform_power.h"
#include "Platform/sdram/platform_sdram.h"
#include "Platform/touch/platform_touch.h"

#include "stm32h7xx_hal.h"

/**
  * @brief  初始化整机启动所必需的 Platform Module。
  * @retval PLATFORM_OK          初始化成功。
  * @retval PLATFORM_PMIC_ERROR  PMIC 初始化失败。
  * @retval PLATFORM_AUDIO_ERROR 音频供电或音频设备初始化失败。
  * @retval PLATFORM_LCD_ERROR   LCD 电源或显示控制器初始化失败。
  * @retval PLATFORM_TOUCH_ERROR 触摸控制器初始化失败。
  * @note   LCD 和 Touch 共用 ALDO2；因此必须先初始化 LCD，再复位并探测触摸
  *         控制器。整个函数由 app_init() 在 FreeRTOS 调度器启动前调用，内部
  *         硬件稳定等待不会阻塞普通 Task。
  */
Platform_StatusTypeDef Platform_Init(void)
{
    /* CubeMX 已配置 FMC；此处把 SDRAM 从上电未知状态推进到可访问状态。 */
    if (Platform_SDRAM_Init() != PLATFORM_SDRAM_OK)
    {
        return PLATFORM_SDRAM_ERROR;
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

    /* LCD 初始化内部开启 ALDO2、完成 ST7789 配置并打开背光。 */
    if (Platform_LCD_Init() != PLATFORM_OK)
    {
        return PLATFORM_LCD_ERROR;
    }

    /* 触摸模组由同一 ALDO2 供电，必须在 LCD 电源已稳定后复位和探测。 */
    if (Platform_Touch_Init() != PLATFORM_OK)
    {
        return PLATFORM_TOUCH_ERROR;
    }

    return PLATFORM_OK;
}
