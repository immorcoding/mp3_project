cmake_minimum_required(VERSION 3.22)

project(volume_host_tests LANGUAGES C)

enable_testing()

# 卷对账与消抖计时的纯函数：不依赖 FreeRTOS 内核，只借共用桩的头文件取 TickType_t。
add_executable(volume_reconcile_tests
    test_volume_reconcile.c
    ../../APP/tasks/volume/volume_reconcile.c
)

target_include_directories(volume_reconcile_tests BEFORE PRIVATE ../stubs/freertos ../..)
target_compile_features(volume_reconcile_tests PRIVATE c_std_11)
target_compile_options(volume_reconcile_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME volume_reconcile COMMAND volume_reconcile_tests)

# 卷事件：链接真实 volume_event.c 加共用 FreeRTOS 桩（假任务与通知计数）。模块状态是静态的，
# 每个用例在独立进程里运行（命令行参数选用例）。
add_executable(volume_event_tests
    test_volume_event.c
    ../../APP/tasks/volume/volume_event.c
    ../stubs/freertos/freertos_stub.c
)

target_include_directories(volume_event_tests BEFORE PRIVATE ../stubs/freertos ../..)
target_compile_features(volume_event_tests PRIVATE c_std_11)
target_compile_options(volume_event_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

foreach(VOLUME_EVENT_CASE
        initial_state sequence wake_slot update_before_wake
        swap_jumps_two late_subscriber subscriber_limit duplicate_subscription published_edge)
    add_test(NAME volume_event_${VOLUME_EVENT_CASE} COMMAND volume_event_tests ${VOLUME_EVENT_CASE})
endforeach()

# 卷任务的 SD 部分：真实 volume_sd.c、volume_reconcile.c、volume_event.c、共用 FreeRTOS 桩与 Service Log 替身；
# Filesystem Service 的七个入口在测试文件里用脚本化的假实现替换。同一份测试源编三次：默认配置、
# 打开 SD 基准（验证基准在发卷就绪之前运行）、打开假抖动钩子（验证节拍与 HOOK 日志）。
foreach(VOLUME_SD_VARIANT default benchmark hook)
    set(VOLUME_SD_TARGET volume_sd_${VOLUME_SD_VARIANT}_tests)
    add_executable(${VOLUME_SD_TARGET}
        test_volume_sd.c
        ../../APP/tasks/volume/sd/volume_sd.c
        ../../APP/tasks/volume/volume_reconcile.c
        ../../APP/tasks/volume/volume_event.c
        ../stubs/freertos/freertos_stub.c
        ../stubs/lock_env/lock_env_log.c
    )
    target_include_directories(${VOLUME_SD_TARGET} BEFORE PRIVATE ../stubs/freertos ../stubs/lock_env ../..)
    target_compile_features(${VOLUME_SD_TARGET} PRIVATE c_std_11)
    target_compile_options(${VOLUME_SD_TARGET} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endforeach()
target_compile_definitions(volume_sd_benchmark_tests PRIVATE STORAGE_SD_BENCHMARK_ENABLE=1)
target_compile_definitions(volume_sd_hook_tests PRIVATE VOLUME_HOOK_FAKE_BOUNCE=1)

add_test(NAME volume_sd COMMAND volume_sd_default_tests)
add_test(NAME volume_sd_benchmark_order COMMAND volume_sd_benchmark_tests)
add_test(NAME volume_sd_fake_bounce_hook COMMAND volume_sd_hook_tests)

# Flash 启动流程：真实 volume_flash.c 与 volume_event.c、共用 FreeRTOS 桩与 Service Log 替身；
# Filesystem Service 的 InitFlash、MountFlash、FormatAndMountFlash 在测试文件里用脚本化的假实现替换。
# 任务创建与自删（volume_flash_task.c）只含 FreeRTOS 任务 API，不进主机测试。
add_executable(volume_flash_tests
    test_volume_flash.c
    ../../APP/tasks/volume/flash/volume_flash.c
    ../../APP/tasks/volume/volume_event.c
    ../stubs/freertos/freertos_stub.c
    ../stubs/lock_env/lock_env_log.c
)

target_include_directories(volume_flash_tests BEFORE PRIVATE ../stubs/freertos ../stubs/lock_env ../..)
target_compile_features(volume_flash_tests PRIVATE c_std_11)
target_compile_options(volume_flash_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME volume_flash COMMAND volume_flash_tests)
