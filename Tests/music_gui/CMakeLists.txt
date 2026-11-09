cmake_minimum_required(VERSION 3.22)

project(music_gui_host_tests LANGUAGES C)

enable_testing()

add_executable(gui_music_queue_window_tests
    test_gui_music_queue_window.c
    ../../APP/apps/music/gui/gui_music_queue_window.c
)

target_include_directories(gui_music_queue_window_tests PRIVATE ../..)
target_compile_features(gui_music_queue_window_tests PRIVATE c_std_11)
target_compile_options(gui_music_queue_window_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME gui_music_queue_window_protocol COMMAND gui_music_queue_window_tests)

add_executable(gui_music_transport_tests
    test_gui_music_transport.c
    ../../APP/apps/music/gui/gui_music_transport.c
)

target_include_directories(gui_music_transport_tests PRIVATE ../..)
target_compile_features(gui_music_transport_tests PRIVATE c_std_11)
target_compile_options(gui_music_transport_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME gui_music_transport_reducer COMMAND gui_music_transport_tests)

add_executable(gui_music_title_tests
    test_gui_music_title.c
    ../../APP/apps/music/gui/gui_music_title.c
)

target_include_directories(gui_music_title_tests PRIVATE ../..)
target_compile_features(gui_music_title_tests PRIVATE c_std_11)
target_compile_options(gui_music_title_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME gui_music_title_client COMMAND gui_music_title_tests)

add_executable(gui_music_queue_rows_tests
    test_gui_music_queue_rows.c
    ../../APP/apps/music/gui/gui_music_queue_rows.c
)

target_include_directories(gui_music_queue_rows_tests PRIVATE ../..)
target_compile_features(gui_music_queue_rows_tests PRIVATE c_std_11)
target_compile_options(gui_music_queue_rows_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME gui_music_queue_rows_columns COMMAND gui_music_queue_rows_tests)

add_executable(gui_music_seek_tests
    test_gui_music_seek.c
    ../../APP/apps/music/gui/gui_music_transport.c
)

target_include_directories(gui_music_seek_tests PRIVATE ../..)
target_compile_features(gui_music_seek_tests PRIVATE c_std_11)
target_compile_options(gui_music_seek_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME gui_music_seek_reducer COMMAND gui_music_seek_tests)
