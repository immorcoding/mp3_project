cmake_minimum_required(VERSION 3.22)

project(playback_task_host_tests LANGUAGES C)

enable_testing()

# 被测生产代码：纯 C 状态机；不链接 FreeRTOS、HAL、Storage 或真实播放模块，二者由 playback_task_test_support.c 的替身代替。
set(PLAYBACK_TASK_PRODUCTION_SOURCES
    ../../APP/tasks/playback/playback_machine.c
)

function(add_playback_task_test test_name source)
    add_executable(${test_name}
        ${source}
        playback_task_test_support.c
        ${PLAYBACK_TASK_PRODUCTION_SOURCES}
    )

    target_include_directories(${test_name} PRIVATE ../..)
    target_compile_features(${test_name} PRIVATE c_std_11)
    target_compile_options(${test_name} PRIVATE -Wall -Wextra -Werror -UNDEBUG)

    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_playback_task_test(playback_task_commands test_playback_task_commands.c)
add_playback_task_test(playback_task_cursor test_playback_task_cursor.c)
add_playback_task_test(playback_task_stream test_playback_task_stream.c)
add_playback_task_test(playback_task_stream_end test_playback_task_stream_end.c)
