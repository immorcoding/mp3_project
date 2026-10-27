/**
  ******************************************************************************
  * @file    resource_sim.c
  * @brief   外部资源区替身：提供 SDRAM 资源区，并模拟 Resource Service 安装与字体查询。
  *
  * @details
  *          固件中 vinyl 区、壁纸区、FONT 区与 GUI 字体解码区由链接脚本在 SDRAM 预留，启动时
  *          Service/resource 从资源包拷入。模拟器用 asm 定义与固件同名的起止符号、定义壁纸像素
  *          数组，再把 Resources/imgs 的打包源数组与 Resources/fonts 的同一份字体 bin（构建期生成
  *          的字节数组）拷入，效果等同安装完成。Service_Resource_GetFont() 按生产头返回 FONT 区
  *          的起点与长度；“字体不可用”开关让它像资源包 v2（无 FONT 条目）一样报告不可用。
  ******************************************************************************
  */

#include "fakes/resource_sim.h"

#include <stdint.h>
#include <string.h>

#include "lvgl.h"

#include "Service/gui/view/gui_service_view_wallpaper.h"
#include "Service/resource/resource_service.h"

/* 打包源本身不含任何 include（资源包工具只取数组），在此提供 LVGL 类型后直接编入。 */
#include "Resources/imgs/vinyl_original_144px.c"
#include "Resources/imgs/ui_img_wallpaper_indigo_mist_soft_dark_png.c"

/** @brief 与 STM32H743xG_flash.ld 中的 vinyl 区大小一致。 */
#define SIM_RESOURCE_VINYL_SIZE  0xF300
/** @brief 与 STM32H743xG_flash.ld 中的 FONT 区大小一致。 */
#define SIM_RESOURCE_FONT_SIZE   0x94800

__asm__(
    ".data\n"
    ".balign 32\n"
    ".globl __external_resource_vinyl_start__\n"
    "__external_resource_vinyl_start__:\n"
    ".space 0xF300\n"
    ".globl __external_resource_vinyl_end__\n"
    "__external_resource_vinyl_end__:\n"
    ".text\n");

/* FONT 区与 GUI 字体解码区放 .bss，不让可执行文件膨胀；大小与链接脚本一致。 */
__asm__(
    ".bss\n"
    ".balign 32\n"
    ".globl __external_resource_font_start__\n"
    "__external_resource_font_start__:\n"
    ".space 0x94800\n"
    ".globl __external_resource_font_end__\n"
    "__external_resource_font_end__:\n"
    ".balign 32\n"
    ".globl __gui_font_decode_start__\n"
    "__gui_font_decode_start__:\n"
    ".space 0x98000\n"
    ".globl __gui_font_decode_end__\n"
    "__gui_font_decode_end__:\n"
    ".text\n");

extern uint8_t __external_resource_vinyl_start__[];
extern uint8_t __external_resource_vinyl_end__[];
extern uint8_t __external_resource_font_start__[];
extern uint8_t __external_resource_font_end__[];

/** @brief 构建期由 cmake/embed_bin.cmake 从 Resources/fonts 的字体 bin 生成。 */
extern const uint8_t sim_font_bin[];
extern const uint32_t sim_font_bin_size;

/** @brief 壁纸像素区；固件中对应 gui_service_view_wallpaper_region.c。 */
uint8_t service_gui_view_wallpaper_pixels[SERVICE_GUI_VIEW_WALLPAPER_BYTES];

/** @brief 已安装的字体载荷长度；0 表示不可用。 */
static uint32_t sim_resource_font_length;

/**
 * @brief 把打包源与字体 bin 拷入资源区，等同 Resource Service 安装完成。
 * @param[in] font_available 为假时不安装字体，字体查询报告不可用。
 * @return 资源区或源数据大小不符时为 false。
 */
bool sim_resource_install(bool font_available)
{
    const uint32_t region = (uint32_t)(__external_resource_vinyl_end__ -
                                       __external_resource_vinyl_start__);
    const uint32_t font_region = (uint32_t)(__external_resource_font_end__ -
                                            __external_resource_font_start__);

    sim_resource_font_length = 0U;

    if ((region != SIM_RESOURCE_VINYL_SIZE) ||
        (font_region != SIM_RESOURCE_FONT_SIZE) ||
        (sim_font_bin_size > font_region) ||
        (vinyl_original_144px.data_size != region) ||
        (ui_img_wallpaper_indigo_mist_soft_dark_png.data_size != SERVICE_GUI_VIEW_WALLPAPER_BYTES))
    {
        return false;
    }

    memcpy(__external_resource_vinyl_start__, vinyl_original_144px.data, region);
    memcpy(service_gui_view_wallpaper_pixels,
           ui_img_wallpaper_indigo_mist_soft_dark_png.data,
           SERVICE_GUI_VIEW_WALLPAPER_BYTES);

    if (font_available)
    {
        memcpy(__external_resource_font_start__, sim_font_bin, sim_font_bin_size);
        sim_resource_font_length = sim_font_bin_size;
    }

    return true;
}

/**
 * @brief 同生产头：查询可选字体载荷是否可用及其在 FONT 区中的位置。
 * @param[out] font 接收可用标志、起点与长度。
 * @retval SERVICE_OK 已返回查询结果。
 * @retval SERVICE_INVALID_PARAM 输出指针为空。
 */
Service_StatusTypeDef Service_Resource_GetFont(Service_ResourceFontTypeDef *font)
{
    if (font == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }

    font->IsAvailable = (sim_resource_font_length != 0U);
    font->Data = font->IsAvailable ? __external_resource_font_start__ : NULL;
    font->Length = sim_resource_font_length;
    return SERVICE_OK;
}
