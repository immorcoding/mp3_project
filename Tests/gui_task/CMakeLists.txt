cmake_minimum_required(VERSION 3.22)

project(gui_task_queue_window_tests LANGUAGES C)

enable_testing()

add_executable(gui_task_queue_window_tests
    test_gui_task_queue_window.c
    ../../APP/tasks/gui/gui_task_queue_window.c
)

target_include_directories(gui_task_queue_window_tests PRIVATE ../..)
target_compile_features(gui_task_queue_window_tests PRIVATE c_std_11)
target_compile_options(gui_task_queue_window_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME gui_task_queue_window_protocol COMMAND gui_task_queue_window_tests)

