cmake_minimum_required(VERSION 3.22)

project(playback_service_host_tests LANGUAGES C)

enable_testing()

# 被测生产代码：播放模块与真实 minimp3，不链接 FreeRTOS、HAL 或 Platform。
set(PLAYBACK_SERVICE_PRODUCTION_SOURCES
    ../../Service/playback/playback_service.c
    ../../Service/playback/playback_service_decoder.c
    ../../Service/playback/playback_service_decoder_minimp3.c
)

function(add_playback_service_test test_name source)
    add_executable(${test_name}
        ${source}
        playback_service_test_support.c
        ${PLAYBACK_SERVICE_PRODUCTION_SOURCES}
    )

    target_include_directories(${test_name} PRIVATE ../..)
    target_compile_features(${test_name} PRIVATE c_std_11)
    target_compile_options(${test_name} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_compile_definitions(${test_name} PRIVATE
        PLAYBACK_SERVICE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )

    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_playback_service_test(playback_service_stream test_playback_service_stream.c)
add_playback_service_test(playback_service_sync test_playback_service_sync.c)
add_playback_service_test(playback_service_info test_playback_service_info.c)
