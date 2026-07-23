#include "BSP/Board/board.h"

#include "BSP/Board/audio/board_audio.h"
#include "BSP/Board/pmic/board_pmic.h"

#include "stm32h7xx_hal.h"

/**
  * @brief  初始化整机启动所必需的 Board Module。
  * @retval BOARD_OK          初始化成功。
  * @retval BOARD_PMIC_ERROR  PMIC 初始化失败。
  * @retval BOARD_AUDIO_ERROR 音频供电或音频设备初始化失败。
  */
Board_StatusTypeDef Board_Init(void)
{
    /* 初始化 PMIC */
    if (Board_PMIC_Init() != BOARD_OK)
    {
        // Handle PMIC initialization error
        return BOARD_PMIC_ERROR;
    }

    /* AUDIO_POWER 由 AXP2101 ALDO1 供电，必须在音频设备初始化之前开启。 */
    if (Board_Audio_SetPower(true) != BOARD_OK)
    {
        return BOARD_AUDIO_ERROR;
    }

    //等待供电稳定
    HAL_Delay(500);

    /* 初始化 Audio */
    if (Board_Audio_Init() != BOARD_OK)
    {
        // Handle Audio initialization error
        return BOARD_AUDIO_ERROR;
    }

    return BOARD_OK;
}
