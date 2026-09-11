cmake_minimum_required(VERSION 3.22)

project(gui_canvas_host_tests LANGUAGES C)

enable_testing()

add_executable(gui_canvas_compose_solid_circles_tests
    test_gui_canvas_compose_solid_circles.c
    stubs/lv_img_cf_get_px_size.c
    ../../Service/gui/canvas/gui_service_canvas_compositor.c
)

target_include_directories(gui_canvas_compose_solid_circles_tests PRIVATE
    stubs
    ../..
)
target_compile_features(gui_canvas_compose_solid_circles_tests PRIVATE c_std_11)
target_compile_options(gui_canvas_compose_solid_circles_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME gui_canvas_compose_solid_circles COMMAND gui_canvas_compose_solid_circles_tests)

add_executable(gui_canvas_compose_image_with_cover_tests
    test_gui_canvas_compose_image_with_cover.c
    stubs/lv_img_cf_get_px_size.c
    ../../Service/gui/canvas/gui_service_canvas_compositor.c
)

target_include_directories(gui_canvas_compose_image_with_cover_tests PRIVATE
    stubs
    ../..
)
target_compile_features(gui_canvas_compose_image_with_cover_tests PRIVATE c_std_11)
target_compile_options(gui_canvas_compose_image_with_cover_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME gui_canvas_compose_image_with_cover COMMAND gui_canvas_compose_image_with_cover_tests)
