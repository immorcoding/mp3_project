#ifndef BOARD_H
#define BOARD_H

typedef enum
{
    BOARD_OK = 0,
    BOARD_PMIC_ERROR,
    BOARD_AUDIO_ERROR
} Board_StatusTypeDef;

Board_StatusTypeDef Board_Init(void);

#endif // BOARD_H