cmake_minimum_required(VERSION 3.22)

project(gui_font_host_tests LANGUAGES C)

enable_testing()

# 两个测试共用同一份解码器源码、最小 lvgl.h 替身和测试工具；不链接 LVGL、FreeRTOS 或 HAL。
set(GUI_FONT_COMMON_SOURCES
    gui_font_test_support.c
    stubs/lv_font_fmt_txt.c
    ../../Service/gui/font/gui_service_font_decoder.c
    ../../Components/resource_pack/resource_pack.c
    ../../Components/resource_pack/resource_pack_binary.c
    ../../Components/resource_pack/resource_pack_font.c
    ../../Components/resource_pack/resource_pack_image.c
)

add_executable(gui_font_decode_synthetic_tests
    test_gui_font_decode_synthetic.c
    ${GUI_FONT_COMMON_SOURCES}
)

add_executable(gui_font_decode_committed_tests
    test_gui_font_decode_committed.c
    ${GUI_FONT_COMMON_SOURCES}
)
target_compile_definitions(gui_font_decode_committed_tests PRIVATE
    TEST_FONT_BIN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../Resources/fonts/noto_sans_cjk_sc_12_4bpp.bin"
)

foreach(target gui_font_decode_synthetic_tests gui_font_decode_committed_tests)
    target_include_directories(${target} PRIVATE
        stubs
        .
        ../..
    )
    target_compile_features(${target} PRIVATE c_std_11)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endforeach()

add_test(NAME gui_font_decode_synthetic COMMAND gui_font_decode_synthetic_tests)
add_test(NAME gui_font_decode_committed COMMAND gui_font_decode_committed_tests)
