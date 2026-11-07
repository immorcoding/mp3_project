cmake_minimum_required(VERSION 3.22)

project(foreground_task_host_tests LANGUAGES C)

enable_testing()

# 前台任务一圈的判断与注册表遍历：真实 foreground_loop.c、app_table_boot.c、共用 FreeRTOS 桩（任务通知、tick），
# 脚本化的周期计数器与 Service Log 替身（Tests/stubs/lock_env/）；桩目录放在 ../.. 之前，让产品的 FreeRTOS include
# 路径取到桩（见 Tests/stubs/freertos/README.md）。
add_executable(foreground_loop_tests
    test_foreground_loop.c
    ../../APP/tasks/foreground/foreground_loop.c
    ../../APP/apps/app_table_boot.c
    ../stubs/freertos/freertos_stub.c
    ../stubs/lock_env/lock_env_cycle_counter.c
    ../stubs/lock_env/lock_env_log.c
)

target_include_directories(foreground_loop_tests BEFORE PRIVATE ../stubs/freertos ../stubs/lock_env ../..)
target_compile_features(foreground_loop_tests PRIVATE c_std_11)
target_compile_options(foreground_loop_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME foreground_loop COMMAND foreground_loop_tests)
