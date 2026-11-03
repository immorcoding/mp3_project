cmake_minimum_required(VERSION 3.22)

project(ui_cell_host_tests LANGUAGES C)

enable_testing()

# 被测生产代码：APP/ui_cell 的真实实现。FreeRTOS 头与周期计数器、tick、任务通知替身沿用 Tests/flash_ftl/stubs；
# 主机是单线程的，“另一个任务正在持锁”由测试自己先拿住格子里的锁来模拟。
add_executable(ui_cell_tests
    test_ui_cell.c
    ../../APP/ui_cell/ui_cell.c
    ../flash_ftl/stubs/filesystem_lock_host.c
)

target_include_directories(ui_cell_tests BEFORE PRIVATE ../flash_ftl/stubs ../..)
target_compile_features(ui_cell_tests PRIVATE c_std_11)
target_compile_options(ui_cell_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME ui_cell_behavior COMMAND ui_cell_tests)
