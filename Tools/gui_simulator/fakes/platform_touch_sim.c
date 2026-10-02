/**
  ******************************************************************************
  * @file    platform_touch_sim.c
  * @brief   Platform Touch 替身：把 SDL 鼠标左键映射为原始触点。
  ******************************************************************************
  */

#include "Platform/touch/platform_touch.h"

#include <stddef.h>

#include "sim_display.h"

bool Platform_Touch_IsAvailable(void)
{
    return true;
}

Platform_StatusTypeDef Platform_Touch_ReadRawPoint(
    Platform_Touch_RawPointTypeDef *point)
{
    if (point == NULL)
    {
        return PLATFORM_TOUCH_ERROR;
    }

    sim_display_get_pointer(&point->IsPressed, &point->X, &point->Y);
    return PLATFORM_OK;
}
