#ifndef BOARD_SD_H
#define BOARD_SD_H

#include <stdint.h>

#include "BSP/Board/board.h"

typedef enum
{
    BOARD_SD_STATE_RESET = 0,
    BOARD_SD_STATE_NOT_PRESENT,
    BOARD_SD_STATE_READY,
    BOARD_SD_STATE_ERROR
} Board_SD_StateTypeDef;

/**
 * @brief sd卡相关参数
 * 
 */
typedef struct
{
    uint64_t CapacityBytes; //容量
    uint32_t BlockCount;    //块/扇区数
    uint32_t BlockSize;     //单个块/扇区容量
} Board_SD_InfoTypeDef;

typedef struct
{
    Board_SD_InfoTypeDef Info;
    bool IsInfoValid;
    Board_SD_StateTypeDef State;
} Board_SD_HandleTypeDef;

Board_StatusTypeDef Board_SD_Init(void);
Board_StatusTypeDef Board_SD_GetInfo(Board_SD_InfoTypeDef *info);

#endif /* BOARD_SD_H */
