/**
  ******************************************************************************
  * @file    gui_service_theme_config.h
  * @brief   GUI 调色板占位 hex、Default/Solid RGB 与 Solid Tab 薄层透明度。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_THEME_CONFIG_H
#define GUI_SERVICE_THEME_CONFIG_H

/* gui_service_theme.c */
#define SERVICE_GUI_THEME_PLACEHOLDER_ACCENT  0x00B0DEU  /* 占位 Accent #00B0DE 青蓝：进度、当前行、Tab 选中、电池填充。 */
#define SERVICE_GUI_THEME_PLACEHOLDER_INK     0xF1F6FFU  /* 占位 Ink #F1F6FF 近白：主文字、浅轮廓、图标。 */
#define SERVICE_GUI_THEME_PLACEHOLDER_MUTED   0x404040U  /* 占位 Muted #404040 深灰：非活动轨道。 */
#define SERVICE_GUI_THEME_PLACEHOLDER_WASH    0xE7E7E7U  /* 占位 Wash #E7E7E7 浅灰：薄填充；Opa 仍写在对象上。 */
#define SERVICE_GUI_THEME_PLACEHOLDER_GROUND  0x13223DU  /* 占位 Ground #13223D 深蓝：纯色页底。 */

#define SERVICE_GUI_THEME_DEFAULT_ACCENT  0x00B0DEU  /* Default Accent #00B0DE 青蓝。 */
#define SERVICE_GUI_THEME_DEFAULT_INK     0xF1F6FFU     /* Default Ink #F1F6FF 近白。 */
#define SERVICE_GUI_THEME_DEFAULT_MUTED   0x404040U   /* Default Muted #404040 深灰。 */
#define SERVICE_GUI_THEME_DEFAULT_WASH    0xE7E7E7U    /* Default Wash #E7E7E7 浅灰。 */
#define SERVICE_GUI_THEME_DEFAULT_GROUND  0x13223DU  /* Default Ground #13223D 深蓝。 */

#define SERVICE_GUI_THEME_SOLID_ACCENT  0xF28ACFU  /* Solid Accent #F28ACF 粉色。 */
#define SERVICE_GUI_THEME_SOLID_INK     0xF4F4F5U  /* Solid Ink #F4F4F5 近白。 */
#define SERVICE_GUI_THEME_SOLID_MUTED   0x71717AU  /* Solid Muted #71717A 中灰。 */
#define SERVICE_GUI_THEME_SOLID_WASH    0xE4E4E7U  /* Solid Wash #E4E4E7 浅灰。 */
#define SERVICE_GUI_THEME_SOLID_GROUND  0x18181BU  /* Solid Ground #18181B 近黑。 */

/* gui_service_theme_apply.c */
#define SERVICE_GUI_THEME_SOLID_TABS_WASH_OPA  (40U)  /* Solid 下 Music Tab 半透明 Wash，不算模糊。 */

#endif /* GUI_SERVICE_THEME_CONFIG_H */
