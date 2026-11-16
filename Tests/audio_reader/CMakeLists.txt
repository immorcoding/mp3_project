cmake_minimum_required(VERSION 3.22)

project(audio_reader_tests LANGUAGES C)

enable_testing()

# 越过标签与跳转偏移钳位（纯函数，保留自 0.9.0 的 storage_audio_range）。
add_executable(audio_reader_range_tests
    test_audio_reader_range.c
    ../../APP/tasks/audio_reader/audio_reader_range.c
)

target_include_directories(audio_reader_range_tests PRIVATE ../..)
target_compile_features(audio_reader_range_tests PRIVATE c_std_11)
target_compile_options(audio_reader_range_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME audio_reader_range_locate COMMAND audio_reader_range_tests)

# 失败结果加卷序号 → 写什么停止原因（纯函数）：读块、开流、拔卡收尾、SERVICE_BUSY 重试与日志节拍、两行新日志的格式。
# audio_reader_stream.h 带 FreeRTOS 头，用共用桩的头文件取到类型，不链接桩。
add_executable(audio_reader_result_tests
    test_audio_reader_result.c
    ../../APP/tasks/audio_reader/audio_reader_result.c
    ../../APP/tasks/audio_reader/audio_reader_diag.c
)

target_include_directories(audio_reader_result_tests BEFORE PRIVATE ../stubs/freertos ../..)
target_compile_features(audio_reader_result_tests PRIVATE c_std_11)
target_compile_options(audio_reader_result_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME audio_reader_result COMMAND audio_reader_result_tests)

# “决定等多久”：水位、让路、一直等、新请求与 SERVICE_BUSY 重试时刻的优先关系（纯函数，不依赖 FreeRTOS）。
add_executable(audio_reader_pace_tests
    test_audio_reader_pace.c
    ../../APP/tasks/audio_reader/audio_reader_pace.c
)

target_include_directories(audio_reader_pace_tests PRIVATE ../..)
target_compile_features(audio_reader_pace_tests PRIVATE c_std_11)
target_compile_options(audio_reader_pace_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME audio_reader_pace COMMAND audio_reader_pace_tests)
