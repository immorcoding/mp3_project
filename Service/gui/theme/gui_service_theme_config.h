/**
  ******************************************************************************
  * @file    gui_service_theme_config.h
  * @brief   GUI Default/Solid 调色板 RGB 与 Solid Tab 薄层透明度。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_THEME_CONFIG_H
#define GUI_SERVICE_THEME_CONFIG_H

/* gui_service_theme.c */
#define SERVICE_GUI_THEME_DEFAULT_ACCENT  0x00B0DEU  /* Default Accent #00B0DE 青蓝。 */
#define SERVICE_GUI_THEME_DEFAULT_INK     0xF1F6FFU  /* Default Ink #F1F6FF 近白。 */
#define SERVICE_GUI_THEME_DEFAULT_MUTED   0x404040U  /* Default Muted #404040 深灰。 */
#define SERVICE_GUI_THEME_DEFAULT_WASH    0xE7E7E7U  /* Default Wash #E7E7E7 浅灰。 */
#define SERVICE_GUI_THEME_DEFAULT_GROUND  0x13223DU  /* Default Ground #13223D 深蓝。 */

#define SERVICE_GUI_THEME_SOLID_ACCENT  0xF28ACFU  /* Solid Accent #F28ACF 粉色。 */
#define SERVICE_GUI_THEME_SOLID_INK     0xF4F4F5U  /* Solid Ink #F4F4F5 近白。 */
#define SERVICE_GUI_THEME_SOLID_MUTED   0x71717AU  /* Solid Muted #71717A 中灰。 */
#define SERVICE_GUI_THEME_SOLID_WASH    0xE4E4E7U  /* Solid Wash #E4E4E7 浅灰。 */
#define SERVICE_GUI_THEME_SOLID_GROUND  0x18181BU  /* Solid Ground #18181B 近黑。 */

/* main/background/gui_service_main_background.c */
#define SERVICE_GUI_THEME_SOLID_TABS_WASH_OPA  (40U)  /* Solid 下 Music Tab 半透明 Wash，不算模糊。 */

#endif /* GUI_SERVICE_THEME_CONFIG_H */
