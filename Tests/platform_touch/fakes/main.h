#ifndef MAIN_H
#define MAIN_H

#include "stm32h7xx_hal.h"

extern GPIO_TypeDef test_tp_rst_gpio_port;

#define TP_RST_GPIO_Port  (&test_tp_rst_gpio_port)
#define TP_RST_Pin         0x0001u

#endif /* MAIN_H */
