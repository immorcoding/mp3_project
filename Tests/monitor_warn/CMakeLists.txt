cmake_minimum_required(VERSION 3.22)

project(monitor_warn_tests LANGUAGES C)

enable_testing()

add_executable(monitor_warn_tests
    test_monitor_warn.c
    ../../APP/tasks/monitor/monitor_warn.c
)

target_include_directories(monitor_warn_tests PRIVATE ../..)
target_compile_features(monitor_warn_tests PRIVATE c_std_11)
target_compile_options(monitor_warn_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME monitor_warn_dedup COMMAND monitor_warn_tests)
