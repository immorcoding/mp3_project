#ifndef BOARD_PMIC_H
#define BOARD_PMIC_H

#include <stdint.h>
#include <stdbool.h>

#include "BSP/Board/board.h"

Board_StatusTypeDef Board_Audio_SetPower(bool enabled);
Board_StatusTypeDef Board_LCD_SetPower(bool enabled);

Board_StatusTypeDef Board_PMIC_Init(void);

#endif // BOARD_H
