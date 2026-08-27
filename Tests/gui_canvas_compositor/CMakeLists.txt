cmake_minimum_required(VERSION 3.22)

project(gui_canvas_compositor_test LANGUAGES C)

include(CTest)

add_executable(gui_canvas_compositor_test
    test_gui_canvas_compositor.c
    ../../Service/gui/canvas/gui_service_canvas_compositor.c
)

target_compile_features(gui_canvas_compositor_test PRIVATE c_std_11)

target_include_directories(gui_canvas_compositor_test PRIVATE
    fakes
    ../..
)

add_test(
    NAME gui_canvas_compositor
    COMMAND gui_canvas_compositor_test
)

add_executable(gui_canvas_effect_test
    test_gui_canvas_effect.c
    ../../Service/gui/canvas/gui_service_canvas.c
)

target_compile_features(gui_canvas_effect_test PRIVATE c_std_11)

target_include_directories(gui_canvas_effect_test PRIVATE
    fakes
    ../..
)

add_test(
    NAME gui_canvas_effect
    COMMAND gui_canvas_effect_test
)
