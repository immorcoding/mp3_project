cmake_minimum_required(VERSION 3.22)

project(gui_theme_palette_tests LANGUAGES C)

enable_testing()

add_executable(gui_theme_palette_tests
    test_gui_theme_palette.c
    ../../Service/gui/theme/gui_service_theme.c
)

target_include_directories(gui_theme_palette_tests PRIVATE ../..)
target_compile_features(gui_theme_palette_tests PRIVATE c_std_11)
target_compile_options(gui_theme_palette_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME gui_theme_palette_mapping COMMAND gui_theme_palette_tests)
