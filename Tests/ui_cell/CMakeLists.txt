cmake_minimum_required(VERSION 3.22)

project(ui_cell_host_tests LANGUAGES C)

enable_testing()

# 真实 ui_cell.c 加共用 FreeRTOS 桩（互斥锁、任务句柄）；桩目录放在 ../.. 之前，让产品的
# FreeRTOS include 路径取到桩（见 Tests/stubs/freertos/README.md）。
add_executable(ui_cell_tests
    test_ui_cell.c
    ../../APP/ui_cell/ui_cell.c
    ../stubs/freertos/freertos_stub.c
)

target_include_directories(ui_cell_tests BEFORE PRIVATE ../stubs/freertos ../..)
target_compile_features(ui_cell_tests PRIVATE c_std_11)
target_compile_options(ui_cell_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME ui_cell_behavior COMMAND ui_cell_tests)
