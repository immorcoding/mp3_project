#include "board.h"
#include "System/Log/log.h"

#include "BSP/Board/audio/board_audio.h"
#include "BSP/Board/pmic/board_pmic.h"
#include "BSP/Board/sd/board_sd.h"

#include "sd/board_sd.h"
#include "stm32h7xx_hal.h"

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
    if (Board_Audio_Init() != AUDIO_OK)
    {
        // Handle Audio initialization error
        return BOARD_AUDIO_ERROR;
    }

    if (Board_SD_Init() != BOARD_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                     "SD",
                     "SD card init failed");
    }

    Board_SD_InfoTypeDef sd_info;
    if (Board_SD_GetInfo(&sd_info) != BOARD_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                     "SD",
                     "SD card get info failed");
    }

    uint32_t capacity_mb = (uint32_t)(sd_info.CapacityBytes / (1024ULL * 1024ULL));

    (void)LOG_Printf(LOG_LEVEL_INFO,
                    "SD",
                    "Card capacity: %lu MB, block size: %lu",
                    (unsigned long)capacity_mb,
                    (unsigned long)sd_info.BlockSize);

    return BOARD_OK;
}
