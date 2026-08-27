#ifndef STM32H7XX_HAL_H
#define STM32H7XX_HAL_H

#include <stdint.h>

typedef struct
{
    uint32_t Reserved;
} I2C_HandleTypeDef;

typedef struct
{
    uint32_t Reserved;
} GPIO_TypeDef;

typedef enum
{
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;

#endif /* STM32H7XX_HAL_H */
