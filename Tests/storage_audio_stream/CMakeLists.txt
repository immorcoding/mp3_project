cmake_minimum_required(VERSION 3.22)

project(storage_audio_stream_tests LANGUAGES C)

enable_testing()

add_executable(storage_audio_stream_tests
    test_storage_audio_range.c
    ../../APP/tasks/storage/stream/storage_audio_range.c
)

target_include_directories(storage_audio_stream_tests PRIVATE ../..)
target_compile_features(storage_audio_stream_tests PRIVATE c_std_11)
target_compile_options(storage_audio_stream_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME storage_audio_range_locate COMMAND storage_audio_stream_tests)

# 音频流对文件调用结果的分类（纯函数）：卷不可用的三种返回值不是文件错误（#211）。
# storage_audio_stream.h 带 FreeRTOS 头，用共用桩的头文件取到类型，不链接桩。
add_executable(storage_audio_result_tests
    test_storage_audio_result.c
    ../../APP/tasks/storage/stream/storage_audio_result.c
)

target_include_directories(storage_audio_result_tests BEFORE PRIVATE ../stubs/freertos ../..)
target_compile_features(storage_audio_result_tests PRIVATE c_std_11)
target_compile_options(storage_audio_result_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME storage_audio_result COMMAND storage_audio_result_tests)
