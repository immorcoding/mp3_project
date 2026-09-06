/**
 ******************************************************************************
 * @file    test_gui_theme_palette.c
 * @brief   GUI 调色板占位映射与外观索引的主机行为测试。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "Service/gui/theme/gui_service_theme.h"
#include "Service/gui/theme/gui_service_theme_config.h"

/**
 * @brief 上电当前外观是 STARTUP，且 STARTUP 指向 Solid。
 */
static void test_startup_current_is_solid(void)
{
    assert(SERVICE_GUI_THEME_STARTUP == SERVICE_GUI_THEME_SOLID);
    assert(service_gui_theme_get_current() == SERVICE_GUI_THEME_SOLID);
}

/**
 * @brief Default 五色宏等于 SquareLine 占位 hex。
 */
static void test_default_palette_macros_match_placeholders(void)
{
    assert(SERVICE_GUI_THEME_DEFAULT_ACCENT == SERVICE_GUI_THEME_PLACEHOLDER_ACCENT);
    assert(SERVICE_GUI_THEME_DEFAULT_INK == SERVICE_GUI_THEME_PLACEHOLDER_INK);
    assert(SERVICE_GUI_THEME_DEFAULT_MUTED == SERVICE_GUI_THEME_PLACEHOLDER_MUTED);
    assert(SERVICE_GUI_THEME_DEFAULT_WASH == SERVICE_GUI_THEME_PLACEHOLDER_WASH);
    assert(SERVICE_GUI_THEME_DEFAULT_GROUND == SERVICE_GUI_THEME_PLACEHOLDER_GROUND);
}

/**
 * @brief Solid 把五个占位 hex 映射到 Solid RGB。
 */
static void test_solid_maps_placeholder_to_palette(void)
{
    assert(service_gui_theme_select(SERVICE_GUI_THEME_SOLID) == SERVICE_OK);
    assert(service_gui_theme_map_placeholder(SERVICE_GUI_THEME_PLACEHOLDER_ACCENT) ==
           SERVICE_GUI_THEME_SOLID_ACCENT);
    assert(service_gui_theme_map_placeholder(SERVICE_GUI_THEME_PLACEHOLDER_INK) ==
           SERVICE_GUI_THEME_SOLID_INK);
    assert(service_gui_theme_map_placeholder(SERVICE_GUI_THEME_PLACEHOLDER_MUTED) ==
           SERVICE_GUI_THEME_SOLID_MUTED);
    assert(service_gui_theme_map_placeholder(SERVICE_GUI_THEME_PLACEHOLDER_WASH) ==
           SERVICE_GUI_THEME_SOLID_WASH);
    assert(service_gui_theme_map_placeholder(SERVICE_GUI_THEME_PLACEHOLDER_GROUND) ==
           SERVICE_GUI_THEME_SOLID_GROUND);
}

/**
 * @brief Default 把五个占位 hex 映射到 Default RGB。
 */
static void test_default_maps_placeholder_to_palette(void)
{
    assert(service_gui_theme_select(SERVICE_GUI_THEME_DEFAULT) == SERVICE_OK);
    assert(service_gui_theme_map_placeholder(SERVICE_GUI_THEME_PLACEHOLDER_ACCENT) ==
           SERVICE_GUI_THEME_DEFAULT_ACCENT);
    assert(service_gui_theme_map_placeholder(SERVICE_GUI_THEME_PLACEHOLDER_INK) ==
           SERVICE_GUI_THEME_DEFAULT_INK);
    assert(service_gui_theme_map_placeholder(SERVICE_GUI_THEME_PLACEHOLDER_MUTED) ==
           SERVICE_GUI_THEME_DEFAULT_MUTED);
    assert(service_gui_theme_map_placeholder(SERVICE_GUI_THEME_PLACEHOLDER_WASH) ==
           SERVICE_GUI_THEME_DEFAULT_WASH);
    assert(service_gui_theme_map_placeholder(SERVICE_GUI_THEME_PLACEHOLDER_GROUND) ==
           SERVICE_GUI_THEME_DEFAULT_GROUND);
}

/**
 * @brief 未知 hex（含透明壳用的白）原样返回。
 */
static void test_unknown_hex_passthrough(void)
{
    assert(service_gui_theme_select(SERVICE_GUI_THEME_SOLID) == SERVICE_OK);
    assert(service_gui_theme_map_placeholder(0xFFFFFFU) == 0xFFFFFFU);
    assert(service_gui_theme_map_placeholder(0x123456U) == 0x123456U);

    assert(service_gui_theme_select(SERVICE_GUI_THEME_DEFAULT) == SERVICE_OK);
    assert(service_gui_theme_map_placeholder(0xFFFFFFU) == 0xFFFFFFU);
    assert(service_gui_theme_map_placeholder(0x123456U) == 0x123456U);
}

/**
 * @brief Solid 关壁纸和毛玻璃；Default 两者都开。
 */
static void test_wallpaper_and_glass_flags(void)
{
    assert(service_gui_theme_select(SERVICE_GUI_THEME_SOLID) == SERVICE_OK);
    assert(service_gui_theme_uses_wallpaper() == false);
    assert(service_gui_theme_uses_glass() == false);

    assert(service_gui_theme_select(SERVICE_GUI_THEME_DEFAULT) == SERVICE_OK);
    assert(service_gui_theme_uses_wallpaper() == true);
    assert(service_gui_theme_uses_glass() == true);
}

/**
 * @brief 非法 id 失败且不改当前外观。
 */
static void test_invalid_id_keeps_current(void)
{
    assert(service_gui_theme_select(SERVICE_GUI_THEME_DEFAULT) == SERVICE_OK);
    assert(service_gui_theme_select(99U) == SERVICE_INVALID_PARAM);
    assert(service_gui_theme_get_current() == SERVICE_GUI_THEME_DEFAULT);
    assert(service_gui_theme_map_placeholder(SERVICE_GUI_THEME_PLACEHOLDER_ACCENT) ==
           SERVICE_GUI_THEME_DEFAULT_ACCENT);
}

/**
 * @brief 运行全部调色板测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_startup_current_is_solid();
    test_default_palette_macros_match_placeholders();
    test_solid_maps_placeholder_to_palette();
    test_default_maps_placeholder_to_palette();
    test_unknown_hex_passthrough();
    test_wallpaper_and_glass_flags();
    test_invalid_id_keeps_current();

    puts("gui_theme_palette_tests: all tests passed");
    return 0;
}
