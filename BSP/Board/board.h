#ifndef BOARD_H
#define BOARD_H

#include "BSP/Devices/audio/audio.h"
#include "BSP/Devices/audio/port/audio_port.h"

#include "BSP/Devices/pmic/pmic.h"
#include "BSP/Devices/pmic/port/pmic_i2c_port.h"

typedef enum
{
    BOARD_OK = 0,
    BOARD_PMIC_ERROR,
    BOARD_AUDIO_ERROR,
    BOARD_LCD_ERROR,
} Board_StatusTypeDef;

Board_StatusTypeDef Board_Init(void);

#endif // BOARD_H
