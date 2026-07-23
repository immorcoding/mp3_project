#ifndef BOARD_AUDIO_H
#define BOARD_AUDIO_H

#include <stdint.h>
#include <stdbool.h>

#include "BSP/Board/board.h"

Board_StatusTypeDef Board_Audio_SetMute(bool mute);
Board_StatusTypeDef Board_Audio_Transmit(const uint16_t *data, uint16_t size);

Board_StatusTypeDef Board_Audio_Init(void);

#endif // BOARD_AUDIO_H
