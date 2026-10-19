cmake_minimum_required(VERSION 3.22)

project(storage_audio_stream_tests LANGUAGES C)

enable_testing()

add_executable(storage_audio_stream_tests
    test_storage_audio_tag.c
    ../../APP/tasks/storage/stream/storage_audio_tag.c
)

target_include_directories(storage_audio_stream_tests PRIVATE ../..)
target_compile_features(storage_audio_stream_tests PRIVATE c_std_11)
target_compile_options(storage_audio_stream_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME storage_audio_tag_locate COMMAND storage_audio_stream_tests)
