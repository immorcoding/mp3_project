/**
  ******************************************************************************
  * @file    sim_display.c
  * @brief   模拟器 SDL 窗口实现。
  ******************************************************************************
  */

#include "sim_display.h"

#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "Platform/lcd/platform_lcd.h"

static SDL_Window *sim_window;
static SDL_Renderer *sim_renderer;
static SDL_Texture *sim_texture;
static uint16_t sim_framebuffer[PLATFORM_LCD_WIDTH * PLATFORM_LCD_HEIGHT];
static bool sim_dirty;

static bool sim_pointer_pressed;
static uint16_t sim_pointer_x;
static uint16_t sim_pointer_y;

bool sim_display_init(bool hidden)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    sim_window = SDL_CreateWindow("MP3 GUI Simulator",
                                  SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED,
                                  (int)PLATFORM_LCD_WIDTH * SIM_DISPLAY_ZOOM,
                                  (int)PLATFORM_LCD_HEIGHT * SIM_DISPLAY_ZOOM,
                                  hidden ? SDL_WINDOW_HIDDEN : 0);
    if (sim_window == NULL)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    sim_renderer = SDL_CreateRenderer(sim_window, -1, SDL_RENDERER_ACCELERATED);
    if (sim_renderer == NULL)
    {
        sim_renderer = SDL_CreateRenderer(sim_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (sim_renderer == NULL)
    {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    /* 产品 lv_conf.h 为 RGB565 且 LV_COLOR_16_SWAP=0，可直接作为纹理格式。 */
    sim_texture = SDL_CreateTexture(sim_renderer,
                                    SDL_PIXELFORMAT_RGB565,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    (int)PLATFORM_LCD_WIDTH,
                                    (int)PLATFORM_LCD_HEIGHT);
    if (sim_texture == NULL)
    {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }

    memset(sim_framebuffer, 0, sizeof(sim_framebuffer));
    sim_dirty = true;
    return true;
}

void sim_display_deinit(void)
{
    if (sim_texture != NULL)
    {
        SDL_DestroyTexture(sim_texture);
    }
    if (sim_renderer != NULL)
    {
        SDL_DestroyRenderer(sim_renderer);
    }
    if (sim_window != NULL)
    {
        SDL_DestroyWindow(sim_window);
    }
    SDL_Quit();
}

void sim_display_write(uint16_t x_start,
                       uint16_t y_start,
                       uint16_t x_end,
                       uint16_t y_end,
                       const uint16_t *pixels)
{
    const uint32_t width = (uint32_t)x_end - x_start + 1U;
    uint32_t y;

    for (y = y_start; y <= y_end; y++)
    {
        memcpy(&sim_framebuffer[y * PLATFORM_LCD_WIDTH + x_start],
               pixels,
               width * sizeof(uint16_t));
        pixels += width;
    }

    sim_dirty = true;
}

void sim_display_present(void)
{
    if (!sim_dirty)
    {
        return;
    }

    SDL_UpdateTexture(sim_texture,
                      NULL,
                      sim_framebuffer,
                      (int)(PLATFORM_LCD_WIDTH * sizeof(uint16_t)));
    SDL_RenderClear(sim_renderer);
    SDL_RenderCopy(sim_renderer, sim_texture, NULL, NULL);
    SDL_RenderPresent(sim_renderer);
    sim_dirty = false;
}

uint32_t sim_display_hash(void)
{
    const uint8_t *bytes = (const uint8_t *)sim_framebuffer;
    uint32_t hash = 2166136261U;
    size_t i;

    /* FNV-1a：只用于判断两帧是否逐像素相同。 */
    for (i = 0; i < sizeof(sim_framebuffer); i++)
    {
        hash ^= bytes[i];
        hash *= 16777619U;
    }

    return hash;
}

bool sim_display_save_bmp(const char *path)
{
    SDL_Surface *surface;
    int result;

    surface = SDL_CreateRGBSurfaceWithFormatFrom(
        sim_framebuffer,
        (int)PLATFORM_LCD_WIDTH,
        (int)PLATFORM_LCD_HEIGHT,
        16,
        (int)(PLATFORM_LCD_WIDTH * sizeof(uint16_t)),
        SDL_PIXELFORMAT_RGB565);
    if (surface == NULL)
    {
        return false;
    }

    result = SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);
    return result == 0;
}

void sim_display_set_pointer(bool pressed, int window_x, int window_y)
{
    sim_display_set_pointer_lcd(pressed,
                                window_x / SIM_DISPLAY_ZOOM,
                                window_y / SIM_DISPLAY_ZOOM);
}

void sim_display_set_pointer_lcd(bool pressed, int x, int y)
{
    if (x < 0)
    {
        x = 0;
    }
    if (y < 0)
    {
        y = 0;
    }

    sim_pointer_pressed = pressed;
    sim_pointer_x = (uint16_t)x;
    sim_pointer_y = (uint16_t)y;
}

void sim_display_get_pointer(bool *pressed, uint16_t *x, uint16_t *y)
{
    *pressed = sim_pointer_pressed;
    *x = sim_pointer_x;
    *y = sim_pointer_y;
}
