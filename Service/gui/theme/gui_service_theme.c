/**
  ******************************************************************************
  * @file    gui_service_theme.c
  * @brief   GUI 外观调色板：角色色值、当前索引与壁纸/毛玻璃标志。
  *
  * @details
  *          不含 LVGL。gui_service_theme_style.c 按本文件的角色色值刷新共享 style；
  *          切外观只换当前表项。
  ******************************************************************************
  */

#include "Service/gui/theme/gui_service_theme.h"
#include "Service/gui/theme/gui_service_theme_config.h"

typedef struct
{
    uint32_t accent;         /**< 强调色。 */
    uint32_t ink;            /**< 主文字与轮廓。 */
    uint32_t muted;          /**< 低对比轨道。 */
    uint32_t wash;           /**< 薄填充。 */
    uint32_t ground;         /**< 纯色页底。 */
    bool uses_wallpaper;     /**< 是否显示 Lock/Main 壁纸图。 */
    bool uses_glass;         /**< 是否走 Music 局部毛玻璃。 */
} Service_GUI_ThemePaletteTypeDef;

static const Service_GUI_ThemePaletteTypeDef service_gui_theme_palettes[] = {
    [SERVICE_GUI_THEME_DEFAULT] = {
        .accent = SERVICE_GUI_THEME_DEFAULT_ACCENT,
        .ink = SERVICE_GUI_THEME_DEFAULT_INK,
        .muted = SERVICE_GUI_THEME_DEFAULT_MUTED,
        .wash = SERVICE_GUI_THEME_DEFAULT_WASH,
        .ground = SERVICE_GUI_THEME_DEFAULT_GROUND,
        .uses_wallpaper = true,
        .uses_glass = true,
    },
    [SERVICE_GUI_THEME_SOLID] = {
        .accent = SERVICE_GUI_THEME_SOLID_ACCENT,
        .ink = SERVICE_GUI_THEME_SOLID_INK,
        .muted = SERVICE_GUI_THEME_SOLID_MUTED,
        .wash = SERVICE_GUI_THEME_SOLID_WASH,
        .ground = SERVICE_GUI_THEME_SOLID_GROUND,
        .uses_wallpaper = false,
        .uses_glass = false,
    },
};

static uint8_t service_gui_theme_current = SERVICE_GUI_THEME_STARTUP;

/**
 * @brief 返回当前外观对应的调色板表项。
 * @return 当前索引对应的只读调色板。
 */
static const Service_GUI_ThemePaletteTypeDef *service_gui_theme_current_palette(void)
{
    return &service_gui_theme_palettes[service_gui_theme_current];
}

/**
 * @brief 读取当前外观索引。
 * @return `SERVICE_GUI_THEME_DEFAULT` 或 `SERVICE_GUI_THEME_SOLID`。
 * @note 上电为 `SERVICE_GUI_THEME_STARTUP`，无需先 select。
 */
uint8_t service_gui_theme_get_current(void)
{
    return service_gui_theme_current;
}

/**
 * @brief 切换当前外观索引，不改 LVGL 对象。
 * @param[in] id `SERVICE_GUI_THEME_DEFAULT` 或 `SERVICE_GUI_THEME_SOLID`。
 * @retval SERVICE_OK 已切换。
 * @retval SERVICE_INVALID_PARAM id 不是已发布的外观。
 * @note 失败时保持原索引。壁纸与毛玻璃由 ThemeApply / Background 读取标志后处理。
 */
Service_StatusTypeDef service_gui_theme_select(uint8_t id)
{
    if ((id != SERVICE_GUI_THEME_DEFAULT) && (id != SERVICE_GUI_THEME_SOLID))
    {
        return SERVICE_INVALID_PARAM;
    }

    service_gui_theme_current = id;
    return SERVICE_OK;
}

/**
 * @brief 读取当前外观下某个角色的 RGB。
 * @param[in] role 调色板角色。
 * @return 24-bit RGB；非法角色返回 0（黑）。
 * @note 不解释 Opa；Opa 写在各对象上。
 */
uint32_t service_gui_theme_role_color(Service_GUI_ThemeRoleTypeDef role)
{
    const Service_GUI_ThemePaletteTypeDef *palette = service_gui_theme_current_palette();

    switch (role)
    {
    case SERVICE_GUI_THEME_ACCENT:
        return palette->accent;
    case SERVICE_GUI_THEME_INK:
        return palette->ink;
    case SERVICE_GUI_THEME_MUTED:
        return palette->muted;
    case SERVICE_GUI_THEME_WASH:
        return palette->wash;
    case SERVICE_GUI_THEME_GROUND:
        return palette->ground;
    default:
        return 0U;
    }
}

/**
 * @brief 当前外观是否显示 Lock/Main 壁纸图。
 * @return Solid 为 false，Default 为 true。
 */
bool service_gui_theme_uses_wallpaper(void)
{
    return service_gui_theme_current_palette()->uses_wallpaper;
}

/**
 * @brief 当前外观是否使用 Music 局部毛玻璃。
 * @return Solid 为 false，Default 为 true。
 */
bool service_gui_theme_uses_glass(void)
{
    return service_gui_theme_current_palette()->uses_glass;
}
