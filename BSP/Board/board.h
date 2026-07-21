#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    BOARD_OK = 0,
    BOARD_PMIC_ERROR,
    BOARD_AUDIO_ERROR,
    BOARD_LCD_ERROR
} Board_StatusTypeDef;

/** @brief 通过 AXP2101 ALDO1 开启或关闭板级音频电源。 */
Board_StatusTypeDef Board_Audio_SetPower(bool enabled);
Board_StatusTypeDef Board_Audio_SetMute(bool mute);
Board_StatusTypeDef Board_Audio_Transmit(
    const uint16_t *data,
    uint16_t size);

/** @brief 通过 AXP2101 ALDO2 开启或关闭板级 LCD 电源。 */
Board_StatusTypeDef Board_LCD_SetPower(bool enabled);

Board_StatusTypeDef Board_Init(void);

#endif // BOARD_H
