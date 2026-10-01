/**
 ******************************************************************************
 * @file    test_gui_theme_palette.c
 * @brief   GUI 调色板角色色值与外观索引的主机行为测试。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "Service/gui/theme/gui_service_theme.h"
#include "Service/gui/theme/gui_service_theme_config.h"

/**
 * @brief 上电外观是 Solid，且当前索引与之一致。
 */
static void test_startup_current_is_solid(void)
{
    assert(SERVICE_GUI_THEME_STARTUP == SERVICE_GUI_THEME_SOLID);
    assert(service_gui_theme_get_current() == SERVICE_GUI_THEME_SOLID);
}

/**
 * @brief Solid 下每个角色返回 Solid 表中的 RGB。
 */
static void test_solid_roles_use_solid_palette(void)
{
    assert(service_gui_theme_select(SERVICE_GUI_THEME_SOLID) == SERVICE_OK);
    assert(service_gui_theme_role_color(SERVICE_GUI_THEME_ACCENT) == SERVICE_GUI_THEME_SOLID_ACCENT);
    assert(service_gui_theme_role_color(SERVICE_GUI_THEME_INK) == SERVICE_GUI_THEME_SOLID_INK);
    assert(service_gui_theme_role_color(SERVICE_GUI_THEME_MUTED) == SERVICE_GUI_THEME_SOLID_MUTED);
    assert(service_gui_theme_role_color(SERVICE_GUI_THEME_WASH) == SERVICE_GUI_THEME_SOLID_WASH);
    assert(service_gui_theme_role_color(SERVICE_GUI_THEME_GROUND) == SERVICE_GUI_THEME_SOLID_GROUND);
}

/**
 * @brief Default 下每个角色返回 Default 表中的 RGB。
 */
static void test_default_roles_use_default_palette(void)
{
    assert(service_gui_theme_select(SERVICE_GUI_THEME_DEFAULT) == SERVICE_OK);
    assert(service_gui_theme_role_color(SERVICE_GUI_THEME_ACCENT) == SERVICE_GUI_THEME_DEFAULT_ACCENT);
    assert(service_gui_theme_role_color(SERVICE_GUI_THEME_INK) == SERVICE_GUI_THEME_DEFAULT_INK);
    assert(service_gui_theme_role_color(SERVICE_GUI_THEME_MUTED) == SERVICE_GUI_THEME_DEFAULT_MUTED);
    assert(service_gui_theme_role_color(SERVICE_GUI_THEME_WASH) == SERVICE_GUI_THEME_DEFAULT_WASH);
    assert(service_gui_theme_role_color(SERVICE_GUI_THEME_GROUND) == SERVICE_GUI_THEME_DEFAULT_GROUND);
}

/**
 * @brief 越界角色返回黑色，不读越界表项。
 */
static void test_invalid_role_is_black(void)
{
    assert(service_gui_theme_select(SERVICE_GUI_THEME_DEFAULT) == SERVICE_OK);
    assert(service_gui_theme_role_color(SERVICE_GUI_THEME_ROLE_COUNT) == 0U);
}

/**
 * @brief Solid 关壁纸与毛玻璃，Default 打开两者。
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
 * @brief 非法外观 id 被拒绝，当前外观与色值不变。
 */
static void test_invalid_id_keeps_current(void)
{
    assert(service_gui_theme_select(SERVICE_GUI_THEME_DEFAULT) == SERVICE_OK);
    assert(service_gui_theme_select(99U) == SERVICE_INVALID_PARAM);
    assert(service_gui_theme_get_current() == SERVICE_GUI_THEME_DEFAULT);
    assert(service_gui_theme_role_color(SERVICE_GUI_THEME_ACCENT) == SERVICE_GUI_THEME_DEFAULT_ACCENT);
}

int main(void)
{
    test_startup_current_is_solid();
    test_solid_roles_use_solid_palette();
    test_default_roles_use_default_palette();
    test_invalid_role_is_black();
    test_wallpaper_and_glass_flags();
    test_invalid_id_keeps_current();

    puts("gui_theme_palette_tests: all tests passed");
    return 0;
}
