cmake_minimum_required(VERSION 3.22)

project(ui_cell_host_tests LANGUAGES C)

enable_testing()

# 真实 ui_cell.c 加共用 FreeRTOS 桩（互斥锁、任务句柄）；桩目录放在 ../.. 之前，让产品的
# FreeRTOS include 路径取到桩（见 Tests/stubs/freertos/README.md）。持锁统计（Debug 构建才有，本测试以 -UNDEBUG 构建）
# 用周期计数器量时间、用 Service Log 出汇总行，由 Tests/stubs/lock_env/ 的脚本化替身提供。
add_executable(ui_cell_tests
    test_ui_cell.c
    ../../APP/ui_cell/ui_cell.c
    ../../APP/ui_cell/ui_cell_stats.c
    ../stubs/freertos/freertos_stub.c
    ../stubs/lock_env/lock_env_cycle_counter.c
    ../stubs/lock_env/lock_env_log.c
)

target_include_directories(ui_cell_tests BEFORE PRIVATE ../stubs/freertos ../stubs/lock_env ../..)
target_compile_features(ui_cell_tests PRIVATE c_std_11)
target_compile_options(ui_cell_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME ui_cell_behavior COMMAND ui_cell_tests)

# 持锁统计：累加、汇总行的格式与节拍（周期计数器和日志都是脚本化的替身）。
add_executable(ui_cell_stats_tests
    test_ui_cell_stats.c
    ../../APP/ui_cell/ui_cell_stats.c
    ../stubs/lock_env/lock_env_cycle_counter.c
    ../stubs/lock_env/lock_env_log.c
)

target_include_directories(ui_cell_stats_tests BEFORE PRIVATE ../stubs/lock_env ../..)
target_compile_features(ui_cell_stats_tests PRIVATE c_std_11)
target_compile_options(ui_cell_stats_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

# 累计值是模块静态的，每个用例一个进程（命令行参数选用例）。
foreach(STATS_CASE hold_max wrap gui_tries poll_period zero_gui)
    add_test(NAME ui_cell_stats_${STATS_CASE} COMMAND ui_cell_stats_tests ${STATS_CASE})
endforeach()

# 发布构建（ui_cell 源码带 -DNDEBUG，与固件 Release 一致）：持锁统计整段编译掉，不量时间、不投日志；
# 读写行为不受开关影响。ui_cell 源码编进 OBJECT 库单独带 -DNDEBUG，测试文件与桩仍用 -UNDEBUG（它们自己的 assert 要保留）。
add_library(ui_cell_release_objects OBJECT
    ../../APP/ui_cell/ui_cell.c
    ../../APP/ui_cell/ui_cell_stats.c
)
target_include_directories(ui_cell_release_objects BEFORE PRIVATE ../stubs/freertos ../stubs/lock_env ../..)
target_compile_features(ui_cell_release_objects PRIVATE c_std_11)
target_compile_options(ui_cell_release_objects PRIVATE -Wall -Wextra -Werror -DNDEBUG)

add_executable(ui_cell_release_tests
    test_ui_cell_release.c
    $<TARGET_OBJECTS:ui_cell_release_objects>
    ../stubs/freertos/freertos_stub.c
    ../stubs/lock_env/lock_env_cycle_counter.c
    ../stubs/lock_env/lock_env_log.c
)
target_include_directories(ui_cell_release_tests BEFORE PRIVATE ../stubs/freertos ../stubs/lock_env ../..)
target_compile_features(ui_cell_release_tests PRIVATE c_std_11)
target_compile_options(ui_cell_release_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME ui_cell_release_has_no_stats COMMAND ui_cell_release_tests)
