cmake_minimum_required(VERSION 3.22)

project(gui_task_host_tests LANGUAGES C)

enable_testing()

add_executable(gui_music_queue_window_tests
    test_gui_music_queue_window.c
    ../../APP/tasks/gui/music/gui_music_queue_window.c
)

target_include_directories(gui_music_queue_window_tests PRIVATE ../..)
target_compile_features(gui_music_queue_window_tests PRIVATE c_std_11)
target_compile_options(gui_music_queue_window_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME gui_music_queue_window_protocol COMMAND gui_music_queue_window_tests)

add_executable(gui_music_transport_tests
    test_gui_music_transport.c
    ../../APP/tasks/gui/music/gui_music_transport.c
)

target_include_directories(gui_music_transport_tests PRIVATE ../..)
target_compile_features(gui_music_transport_tests PRIVATE c_std_11)
target_compile_options(gui_music_transport_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME gui_music_transport_policy COMMAND gui_music_transport_tests)

add_executable(gui_service_input_tests
    test_gui_service_input.c
    ../../Service/gui/gui_service_input.c
)

target_include_directories(gui_service_input_tests PRIVATE ../..)
target_compile_features(gui_service_input_tests PRIVATE c_std_11)
target_compile_options(gui_service_input_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME gui_service_input_mailbox COMMAND gui_service_input_tests)
