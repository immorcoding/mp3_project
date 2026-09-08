/**
 ******************************************************************************
 * @file    lv_img_cf_get_px_size.c
 * @brief   主机测试替身：TRUE_COLOR_ALPHA 在 16-bit 色深下为 24 bit/像素。
 ******************************************************************************
 */

#include "lvgl.h"

/**
 * @brief 返回图片色格式的每像素位数。
 * @param[in] cf 图片色格式。
 * @return 每像素位数；主机测试只覆盖 TRUE_COLOR_ALPHA。
 */
uint8_t lv_img_cf_get_px_size(lv_img_cf_t cf)
{
    if (cf == LV_IMG_CF_TRUE_COLOR_ALPHA)
    {
        return 24U;
    }

    return 0U;
}
