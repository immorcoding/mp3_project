cmake_minimum_required(VERSION 3.22)

project(storage_catalog_cursor_tests LANGUAGES C)

enable_testing()

add_executable(storage_catalog_cursor_tests
    test_storage_playback_cursor.c
    ../../APP/tasks/storage/catalog/storage_playback_cursor.c
)

target_include_directories(storage_catalog_cursor_tests PRIVATE ../..)
target_compile_features(storage_catalog_cursor_tests PRIVATE c_std_11)
target_compile_options(storage_catalog_cursor_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME storage_playback_cursor_protocol COMMAND storage_catalog_cursor_tests)
