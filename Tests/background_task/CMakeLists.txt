cmake_minimum_required(VERSION 3.22)

project(background_task_host_tests LANGUAGES C)

enable_testing()

# 后台任务一圈的判断：真实 background_loop.c 加共用 FreeRTOS 桩（任务通知、tick）；桩目录放在 ../.. 之前，
# 让产品的 FreeRTOS include 路径取到桩（见 Tests/stubs/freertos/README.md）。
add_executable(background_loop_tests
    test_background_loop.c
    ../../APP/tasks/background/background_loop.c
    ../stubs/freertos/freertos_stub.c
)

target_include_directories(background_loop_tests BEFORE PRIVATE ../stubs/freertos ../..)
target_compile_features(background_loop_tests PRIVATE c_std_11)
target_compile_options(background_loop_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME background_loop COMMAND background_loop_tests)

# Flash 压力钩子作业：真实 background_flash_pressure.c、background_jobs.c、background_loop.c、volume_event.c、共用 FreeRTOS 桩与
# Service Log 替身；Filesystem Service 的五个文件入口和 Platform_Flash_GetVolumeDiagnostics 在测试文件里用脚本化的假实现。
# 作业状态和卷事件是静态的，每个用例一个进程（命令行参数选用例）。同一份测试源编四个配置。
foreach(PRESSURE_VARIANT default hook floor noreclaim)
    set(PRESSURE_TARGET background_pressure_${PRESSURE_VARIANT}_tests)
    add_executable(${PRESSURE_TARGET}
        test_background_flash_pressure.c
        ../../APP/tasks/background/background_flash_pressure.c
        ../../APP/tasks/background/background_jobs.c
        ../../APP/tasks/background/background_loop.c
        ../../APP/tasks/volume/volume_event.c
        ../stubs/freertos/freertos_stub.c
        ../stubs/lock_env/lock_env_log.c
    )
    target_include_directories(${PRESSURE_TARGET} BEFORE PRIVATE ../stubs/freertos ../stubs/lock_env ../..)
    target_compile_features(${PRESSURE_TARGET} PRIVATE c_std_11)
    target_compile_options(${PRESSURE_TARGET} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endforeach()
foreach(PRESSURE_VARIANT hook floor noreclaim)
    target_compile_definitions(background_pressure_${PRESSURE_VARIANT}_tests PRIVATE BACKGROUND_HOOK_FLASH_PRESSURE=1)
endforeach()
target_compile_definitions(background_pressure_floor_tests PRIVATE
    BACKGROUND_HOOK_FLASH_PRESSURE_FREE_FLOOR=300U TEST_VARIANT_FLOOR=1)
target_compile_definitions(background_pressure_noreclaim_tests PRIVATE
    BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM=0U TEST_VARIANT_NORECLAIM=1)

add_test(NAME background_pressure_default_off COMMAND background_pressure_default_tests default_build_has_no_job)

foreach(PRESSURE_CASE
        announce table_registers_the_job waits_for_volume one_file_cycle step_return_values
        write_source_and_chunk stats_lines goes_below_low_water diagnostics_failure open_hard_failure
        close_failure remove_failure full_volume no_space_is_full volume_unavailable sequence_change
        write_failure_stops reclaim_failure_stops open_busy_retry open_busy_gives_up)
    add_test(NAME background_pressure_${PRESSURE_CASE} COMMAND background_pressure_hook_tests ${PRESSURE_CASE})
endforeach()
add_test(NAME background_pressure_floor_announce COMMAND background_pressure_floor_tests announce)
add_test(NAME background_pressure_free_floor COMMAND background_pressure_floor_tests free_floor)
add_test(NAME background_pressure_noreclaim_announce COMMAND background_pressure_noreclaim_tests announce)
add_test(NAME background_pressure_noreclaim_cycle COMMAND background_pressure_noreclaim_tests one_file_cycle)
add_test(NAME background_pressure_noreclaim_returns COMMAND background_pressure_noreclaim_tests step_return_values)
add_test(NAME background_pressure_never_reclaims COMMAND background_pressure_noreclaim_tests never_reclaims)
