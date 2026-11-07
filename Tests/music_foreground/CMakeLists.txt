cmake_minimum_required(VERSION 3.22)

project(music_foreground_host_tests LANGUAGES C)

enable_testing()

# 构建目标名故意取短（mf_locmiss_tests、mds_*、mhp_*）：pre-push 把仓库复制到 `%TEMP%\mp3-push-<32 位>\`（根路径约 75 字符，没有 FULL 用的
# 短根映射），目标目录名 `<目标>.dir` 加上跨目录源文件（APP/ui_cell/…）的长对象路径，超过 MAX_PATH（260）就报
# `opening dependency file … .obj.d: No such file or directory`。CTest 测试名（add_test 的 NAME）保持可读，只缩短构建目标名。

# 行缓存：真实 music_row_cache.c，没有任何外部依赖。
add_executable(music_row_cache_tests
    test_music_row_cache.c
    ../../APP/apps/music/foreground/music_row_cache.c
)

target_include_directories(music_row_cache_tests PRIVATE ../..)
target_compile_features(music_row_cache_tests PRIVATE c_std_11)
target_compile_options(music_row_cache_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME music_row_cache COMMAND music_row_cache_tests)

# 音乐前台流程：真实 music_foreground.c、music_row_cache.c、music_cells.c、ui_cell（含持锁统计）、显示延迟汇总、翻页钩子、
# 共用 FreeRTOS 桩（互斥锁、tick、通知）与脚本化的周期计数器和 Service Log 替身（Tests/stubs/lock_env/）；
# 曲库读者、曲目文本读取、读标签诊断、卷边沿时刻、块池就绪数和前台叫醒函数在测试文件里用假实现替换，只实现既有公开函数。
# 桩目录放在 ../.. 之前，让产品的 FreeRTOS include 路径取到桩（见 Tests/stubs/freertos/README.md）。
add_executable(music_foreground_tests
    test_music_foreground.c
    ../../APP/apps/music/foreground/music_foreground.c
    ../../APP/apps/music/foreground/music_row_cache.c
    ../../APP/apps/music/music_cells.c
    ../../APP/apps/music/music_display_stats.c
    ../../APP/apps/music/music_hook_pageturn.c
    ../../APP/ui_cell/ui_cell.c
    ../../APP/ui_cell/ui_cell_stats.c
    ../stubs/freertos/freertos_stub.c
    ../stubs/lock_env/lock_env_cycle_counter.c
    ../stubs/lock_env/lock_env_log.c
)

target_include_directories(music_foreground_tests BEFORE PRIVATE ../stubs/freertos ../stubs/lock_env ../..)
target_compile_features(music_foreground_tests PRIVATE c_std_11)
target_compile_options(music_foreground_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME music_foreground COMMAND music_foreground_tests)

# 显示延迟汇总：真实 music_display_stats.c 加脚本化的 Service Log 替身；累计值是模块静态的，每个用例一个进程。
add_executable(mds_tests
    test_music_display_stats.c
    ../../APP/apps/music/music_display_stats.c
    ../stubs/lock_env/lock_env_log.c
)

target_include_directories(mds_tests BEFORE PRIVATE ../stubs/lock_env ../..)
target_compile_features(mds_tests PRIVATE c_std_11)
target_compile_options(mds_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

foreach(STATS_CASE no_samples period clear wrap extreme)
    add_test(NAME music_display_stats_${STATS_CASE} COMMAND mds_tests ${STATS_CASE})
endforeach()

# 发布构建（显示延迟汇总源码带 -DNDEBUG，与固件 Release 一致）：整段编译掉，调用点的内联空函数什么都不记、什么都不投。
add_library(mds_release_objs OBJECT ../../APP/apps/music/music_display_stats.c)
target_include_directories(mds_release_objs BEFORE PRIVATE ../stubs/lock_env ../..)
target_compile_features(mds_release_objs PRIVATE c_std_11)
target_compile_options(mds_release_objs PRIVATE -Wall -Wextra -Werror -DNDEBUG)

add_executable(mds_release_tests
    test_music_display_stats_release.c
    $<TARGET_OBJECTS:mds_release_objs>
    ../stubs/lock_env/lock_env_log.c
)
target_include_directories(mds_release_tests BEFORE PRIVATE ../stubs/lock_env ../..)
target_compile_features(mds_release_tests PRIVATE c_std_11)
target_compile_options(mds_release_tests PRIVATE -Wall -Wextra -Werror -DNDEBUG)

add_test(NAME music_display_stats_release_is_empty COMMAND mds_release_tests)

# 翻页注入钩子：真实 music_hook_pageturn.c 加脚本化的 Service Log 替身（它包含前台任务的公开头取 FOREGROUND_WAIT_FOREVER，
# 所以带共用 FreeRTOS 桩的头目录）；同一份测试源编两个配置：默认（钩子关）和钩子打开。
# 注入时刻与测量状态是模块静态的，每个用例一个进程。
foreach(HOOK_VARIANT default hook)
    set(HOOK_TARGET mhp_${HOOK_VARIANT}_tests)
    add_executable(${HOOK_TARGET}
        test_music_hook_pageturn.c
        ../../APP/apps/music/music_hook_pageturn.c
        ../stubs/lock_env/lock_env_log.c
    )
    target_include_directories(${HOOK_TARGET} BEFORE PRIVATE ../stubs/freertos ../stubs/lock_env ../..)
    target_compile_features(${HOOK_TARGET} PRIVATE c_std_11)
    target_compile_options(${HOOK_TARGET} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endforeach()
target_compile_definitions(mhp_hook_tests PRIVATE MUSIC_HOOK_PAGE_TURN=1)

add_test(NAME music_hook_pageturn_default_off COMMAND mhp_default_tests default_off)
foreach(HOOK_CASE announce no_injection refill timeout replace)
    add_test(NAME music_hook_pageturn_${HOOK_CASE} COMMAND mhp_hook_tests ${HOOK_CASE})
endforeach()

# 定位符失效钩子打开的配置（AUDIO_READER_HOOK_LOCATOR_MISS=1，R5.5）：同一份测试源，前台的曲名和窗口行把定位符清零再读文件。
add_executable(mf_locmiss_tests
    test_music_foreground.c
    ../../APP/apps/music/foreground/music_foreground.c
    ../../APP/apps/music/foreground/music_row_cache.c
    ../../APP/apps/music/music_cells.c
    ../../APP/apps/music/music_display_stats.c
    ../../APP/apps/music/music_hook_pageturn.c
    ../../APP/ui_cell/ui_cell.c
    ../../APP/ui_cell/ui_cell_stats.c
    ../stubs/freertos/freertos_stub.c
    ../stubs/lock_env/lock_env_cycle_counter.c
    ../stubs/lock_env/lock_env_log.c
)

target_include_directories(mf_locmiss_tests BEFORE PRIVATE ../stubs/freertos ../stubs/lock_env ../..)
target_compile_features(mf_locmiss_tests PRIVATE c_std_11)
target_compile_options(mf_locmiss_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_compile_definitions(mf_locmiss_tests PRIVATE AUDIO_READER_HOOK_LOCATOR_MISS=1)

add_test(NAME music_foreground_locator_miss_hook COMMAND mf_locmiss_tests)
