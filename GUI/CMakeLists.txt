SET(SOURCES screens/ui_Boot.c
    screens/ui_BootReveal.c
    screens/ui_Lock.c
    screens/ui_Main.c
    ui_theme_manager.c
    ui_themes.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_wallpaper_indigo_mist_soft_dark_png.c)

add_library(ui ${SOURCES})
