/**
 ******************************************************************************
 * @file    lv_font_fmt_txt.c
 * @brief   主机测试用的 LVGL 字体回调占位符号。
 *
 * 解码器只把这两个函数的地址写进 lv_font_t，从不调用；测试比较地址即可，不复刻 LVGL 的查找实现。
 ******************************************************************************
 */

#include "lvgl.h"

/**
 * @brief 占位：解码器从不调用。
 * @param[in] font 未使用。
 * @param[out] dsc_out 未使用。
 * @param[in] unicode_letter 未使用。
 * @param[in] unicode_letter_next 未使用。
 * @return 恒为 false。
 */
bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out, uint32_t unicode_letter,
                                   uint32_t unicode_letter_next)
{
    (void)font;
    (void)dsc_out;
    (void)unicode_letter;
    (void)unicode_letter_next;
    return false;
}

/**
 * @brief 占位：解码器从不调用。
 * @param[in] font 未使用。
 * @param[in] letter 未使用。
 * @return 恒为 NULL。
 */
const uint8_t *lv_font_get_bitmap_fmt_txt(const lv_font_t *font, uint32_t letter)
{
    (void)font;
    (void)letter;
    return NULL;
}
