#ifndef BOARD_PMIC_H
#define BOARD_PMIC_H

#include <stdint.h>
#include <stdbool.h>

#include "BSP/Board/board.h"

/** @brief 通过 AXP2101 开启或关闭板级 LCD 电源。 */
Board_StatusTypeDef Board_Audio_SetPower(bool enabled);
Board_StatusTypeDef Board_LCD_SetPower(bool enabled);

Board_StatusTypeDef Board_PMIC_Init(void);

#endif // BOARD_H
