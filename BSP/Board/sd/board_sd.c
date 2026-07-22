#include "BSP/Board/sd/board_sd.h"

#include "sdmmc.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static Board_SD_HandleTypeDef hsd;

/**
 * @brief SD卡初始化，获取容量，初始化SD时钟线等
 * 
 * @return Board_StatusTypeDef 
 */
Board_StatusTypeDef Board_SD_Init(void)
{
    memset(&hsd, 0, sizeof(hsd));

    HAL_SD_CardInfoTypeDef hal_info;

    if (HAL_SD_GetState(&hsd1) != HAL_SD_STATE_READY)
    {
        board_sd_info_valid = false;
        return BOARD_SD_ERROR;
    }

    if (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER)
    {
        board_sd_info_valid = false;
        return BOARD_SD_ERROR;
    }

    if (HAL_SD_GetCardInfo(&hsd1, &hal_info) != HAL_OK)
    {
        board_sd_info_valid = false;
        return BOARD_SD_ERROR;
    }

    board_sd_info.BlockCount = hal_info.LogBlockNbr;
    board_sd_info.BlockSize = hal_info.LogBlockSize;
    board_sd_info.CapacityBytes = (uint64_t)hal_info.LogBlockNbr * (uint64_t)hal_info.LogBlockSize;

    board_sd_info_valid = true;

    return BOARD_OK;
}

Board_StatusTypeDef Board_SD_GetInfo(Board_SD_InfoTypeDef *info)
{
    if ((info == NULL) || (board_sd_info_valid == false))
    {
        return BOARD_SD_ERROR;
    }

    *info = board_sd_info;

    return BOARD_OK;
}
