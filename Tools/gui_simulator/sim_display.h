/**
  ******************************************************************************
  * @file    sim_display.h
  * @brief   模拟器 SDL 窗口：RGB565 帧缓冲、鼠标触点与呈现。
  *
  * @details
  *          Platform LCD/Touch 替身经本接口写像素、读鼠标；sim_main 负责事件泵与呈现。
  ******************************************************************************
  */

#ifndef SIM_DISPLAY_H
#define SIM_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 窗口相对 240x320 LCD 的放大倍数。 */
#define SIM_DISPLAY_ZOOM  2

/** @param hidden 为真时不显示窗口，仅维护帧缓冲（无人值守比对）。 */
bool sim_display_init(bool hidden);
void sim_display_deinit(void);

void sim_display_write(uint16_t x_start,
                       uint16_t y_start,
                       uint16_t x_end,
                       uint16_t y_end,
                       const uint16_t *pixels);

/** @brief 帧缓冲有变化时上传纹理并呈现。 */
void sim_display_present(void);

/** @brief 当前帧缓冲的 FNV-1a 哈希。 */
uint32_t sim_display_hash(void);

/** @brief 把当前帧缓冲保存为 BMP。 */
bool sim_display_save_bmp(const char *path);

/** @brief 以窗口坐标设置鼠标触点（内部按放大倍数换算）。 */
void sim_display_set_pointer(bool pressed, int window_x, int window_y);

/** @brief 以 LCD 坐标设置触点，供脚本输入使用。 */
void sim_display_set_pointer_lcd(bool pressed, int x, int y);
void sim_display_get_pointer(bool *pressed, uint16_t *x, uint16_t *y);

#endif /* SIM_DISPLAY_H */
