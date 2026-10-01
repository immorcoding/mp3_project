/**
  ******************************************************************************
  * @file    sim_clock.c
  * @brief   模拟器时钟实现。
  ******************************************************************************
  */

#include "sim_clock.h"

#include <SDL.h>

static bool sim_clock_deterministic;
static uint32_t sim_clock_virtual_ms;

void sim_clock_set_deterministic(bool deterministic)
{
    sim_clock_deterministic = deterministic;
}

bool sim_clock_is_deterministic(void)
{
    return sim_clock_deterministic;
}

uint32_t sim_clock_now(void)
{
    return sim_clock_deterministic ? sim_clock_virtual_ms : (uint32_t)SDL_GetTicks();
}

void sim_clock_tick(void)
{
    if (sim_clock_deterministic)
    {
        sim_clock_virtual_ms += SIM_CLOCK_STEP_MS;
    }
}
